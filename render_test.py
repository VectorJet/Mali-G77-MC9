import bpy

# Set render engine to Eevee (which uses OpenGL, and therefore Zink -> Venus -> Vulkan)
bpy.context.scene.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items.keys() else 'BLENDER_EEVEE'
bpy.context.scene.render.filepath = '/home/tammy/dev/experiments/Mali-G77-MC9/render.png'

# Render
bpy.ops.render.render(write_still=True)
print("Render complete!")
