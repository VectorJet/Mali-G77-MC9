/*
 * Mali-G77 MC9 (Valhall v9) pan_kmod kbase backend implementation
 * Connects Mesa pan_kmod interfaces directly to hardware /dev/mali0
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <unistd.h>

#include "pan_kmod_kbase.h"
#include "kbase_winsys.h"

struct pan_kmod_dev {
    struct kbase_dev *kdev;
    struct pan_kmod_dev_props props;
};

struct pan_kmod_dev *pan_kmod_dev_create(const char *dev_node) {
    struct kbase_dev *kdev = kbase_dev_open(dev_node);
    if (!kdev) return NULL;

    struct pan_kmod_dev *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        kbase_dev_close(kdev);
        return NULL;
    }

    dev->kdev = kdev;
    dev->props.gpu_id = 0x09000800; /* Mali-G77 r0p1 (Valhall v9) */
    dev->props.gpu_revision = 0x0000;
    dev->props.core_count = 9;
    snprintf(dev->props.ddk_version, sizeof(dev->props.ddk_version), "r49p1-03bet0");

    return dev;
}

void pan_kmod_dev_destroy(struct pan_kmod_dev *dev) {
    if (!dev) return;
    if (dev->kdev) kbase_dev_close(dev->kdev);
    free(dev);
}

int pan_kmod_dev_query_props(struct pan_kmod_dev *dev, struct pan_kmod_dev_props *props) {
    if (!dev || !props) return -EINVAL;
    *props = dev->props;
    return 0;
}

struct pan_kmod_bo_impl {
    struct pan_kmod_bo base;
    struct kbase_bo *kbo;
};

struct pan_kmod_bo *pan_kmod_bo_alloc(struct pan_kmod_dev *dev, size_t size, uint32_t flags) {
    if (!dev || !dev->kdev || size == 0) return NULL;

    uint32_t kflags = 0;
    if (flags & PAN_KMOD_BO_FLAG_READ)     kflags |= KBASE_BO_PROT_READ;
    if (flags & PAN_KMOD_BO_FLAG_WRITE)    kflags |= KBASE_BO_PROT_WRITE;
    if (flags & PAN_KMOD_BO_FLAG_EXEC)     kflags |= KBASE_BO_PROT_EXEC;
    if (flags & PAN_KMOD_BO_FLAG_COHERENT) kflags |= KBASE_BO_COHERENT;

    struct kbase_bo *kbo = kbase_bo_alloc(dev->kdev, size, kflags);
    if (!kbo) return NULL;

    struct pan_kmod_bo_impl *impl = calloc(1, sizeof(*impl));
    if (!impl) {
        kbase_bo_free(kbo);
        return NULL;
    }

    impl->kbo = kbo;
    impl->base.dev = dev;
    impl->base.cpu = kbo->cpu;
    impl->base.gpu = kbo->gpu;
    impl->base.size = kbo->size;
    impl->base.handle = kbo->handle;
    impl->base.flags = flags;

    return &impl->base;
}

void pan_kmod_bo_free(struct pan_kmod_bo *bo) {
    if (!bo) return;
    struct pan_kmod_bo_impl *impl = (struct pan_kmod_bo_impl *)bo;
    if (impl->kbo) {
        kbase_bo_free(impl->kbo);
        impl->kbo = NULL;
    }
    free(impl);
}

int pan_kmod_submit_atom(struct pan_kmod_dev *dev, uint64_t jc_gpu, uint32_t core_req,
                         uint32_t atom_id, uint32_t *event_code) {
    return pan_kmod_submit_atom_timeout(dev, jc_gpu, core_req, atom_id, event_code, -1);
}

int pan_kmod_submit_atom_timeout(struct pan_kmod_dev *dev, uint64_t jc_gpu, uint32_t core_req,
                                 uint32_t atom_id, uint32_t *event_code, int timeout_ms) {
    if (!dev || !dev->kdev) return -EINVAL;

    int ret = kbase_submit_job(dev->kdev, jc_gpu, core_req, atom_id, 0, 0);
    if (ret < 0) return ret;

    uint32_t rx_atom = 0, rx_code = 0;
    if (timeout_ms < 0) {
        ret = kbase_wait_event(dev->kdev, &rx_atom, &rx_code);
    } else {
        ret = kbase_wait_event_timeout(dev->kdev, &rx_atom, &rx_code, timeout_ms);
    }

    if (event_code) *event_code = rx_code;
    if (ret == -EAGAIN) return 0; /* Timeout hit, job still queued/executing */
    return (ret == 0 && rx_code == 0x1) ? 0 : -EIO;
}

int pan_kmod_submit_atoms_chained(struct pan_kmod_dev *dev,
                                 const struct pan_kmod_atom *atoms,
                                 uint32_t nr_atoms,
                                 uint32_t *final_event_code,
                                 int timeout_ms) {
    if (!dev || !dev->kdev || !atoms || nr_atoms == 0) return -EINVAL;

    struct kbase_atom_submit_info *k_info = calloc(nr_atoms, sizeof(struct kbase_atom_submit_info));
    if (!k_info) return -ENOMEM;

    for (uint32_t i = 0; i < nr_atoms; i++) {
        k_info[i].jc = atoms[i].jc_gpu;
        k_info[i].core_req = atoms[i].core_req;
        k_info[i].atom_number = atoms[i].atom_id;
        k_info[i].jobslot = atoms[i].jobslot;
        k_info[i].dep_atom_id[0] = (uint8_t)atoms[i].dep_atom_id[0];
        k_info[i].dep_type[0]    = (uint8_t)atoms[i].dep_type[0];
        k_info[i].dep_atom_id[1] = (uint8_t)atoms[i].dep_atom_id[1];
        k_info[i].dep_type[1]    = (uint8_t)atoms[i].dep_type[1];
    }

    int ret = kbase_submit_atoms(dev->kdev, k_info, nr_atoms);
    free(k_info);
    if (ret < 0) return ret;

    /* Drain completion events for all submitted atoms */
    uint32_t last_code = 0;
    int overall_status = 0;
    for (uint32_t i = 0; i < nr_atoms; i++) {
        uint32_t rx_atom = 0, rx_code = 0;
        int ev_ret;
        if (timeout_ms < 0) {
            ev_ret = kbase_wait_event(dev->kdev, &rx_atom, &rx_code);
        } else {
            ev_ret = kbase_wait_event_timeout(dev->kdev, &rx_atom, &rx_code, timeout_ms);
        }
        if (ev_ret < 0 && overall_status == 0) overall_status = ev_ret;
        bool is_done = (rx_code == 0x1 || rx_code == 0x40);
        if (!is_done && overall_status == 0) overall_status = -EIO;
        last_code = rx_code;
    }

    if (final_event_code) *final_event_code = last_code;
    return overall_status;
}
