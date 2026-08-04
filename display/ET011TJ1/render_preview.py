"""Render an isometric preview of ET011TJ1.stl with Blender."""

from pathlib import Path
import math
import bpy


HERE = Path(__file__).resolve().parent

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
bpy.ops.wm.stl_import(filepath=str(HERE / "ET011TJ1.stl"))
model = bpy.context.active_object
model.name = "ET011TJ1"

mat = bpy.data.materials.new("Graphite")
mat.use_nodes = True
bsdf = mat.node_tree.nodes.get("Principled BSDF")
bsdf.inputs["Base Color"].default_value = (0.46, 0.52, 0.58, 1.0)
bsdf.inputs["Metallic"].default_value = 0.28
bsdf.inputs["Roughness"].default_value = 0.30
model.data.materials.append(mat)

# Keep the drawing orientation and add a slight in-plane turn for depth cues.
model.rotation_euler = (0, 0, math.radians(-18))

ground_mat = bpy.data.materials.new("Ground")
ground_mat.use_nodes = True
ground_bsdf = ground_mat.node_tree.nodes.get("Principled BSDF")
ground_bsdf.inputs["Base Color"].default_value = (0.035, 0.050, 0.072, 1.0)
ground_bsdf.inputs["Roughness"].default_value = 0.72
bpy.ops.mesh.primitive_plane_add(size=180, location=(0, 0, 12))
ground = bpy.context.active_object
ground.data.materials.append(ground_mat)
ground.hide_render = True

bpy.ops.object.light_add(type="AREA", location=(35, -35, -60))
bpy.context.active_object.data.energy = 2100
bpy.context.active_object.data.shape = "DISK"
bpy.context.active_object.data.size = 45
bpy.context.active_object.rotation_euler = (math.pi, 0, 0)
bpy.ops.object.light_add(type="AREA", location=(-40, 15, -35))
bpy.context.active_object.data.energy = 1400
bpy.context.active_object.data.size = 35
bpy.context.active_object.rotation_euler = (math.pi, 0, 0)
bpy.ops.object.light_add(type="AREA", location=(5, 45, -50))
bpy.context.active_object.data.energy = 1100
bpy.context.active_object.data.size = 30
bpy.context.active_object.rotation_euler = (math.pi, 0, 0)
bpy.ops.object.light_add(type="SUN", location=(0, 0, -70))
bpy.context.active_object.rotation_euler = (math.radians(208), math.radians(-22), math.radians(20))
bpy.context.active_object.data.energy = 3.0

bpy.ops.object.camera_add(location=(55, -65, -100))
camera = bpy.context.active_object
bpy.context.scene.camera = camera
camera.data.type = "ORTHO"
camera.data.ortho_scale = 70

def point_camera(obj, target=(0, 7, 2.5)):
    from mathutils import Vector
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat("-Z", "Y").to_euler()

point_camera(camera)

scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE_NEXT"
scene.render.resolution_x = 1400
scene.render.resolution_y = 1100
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.film_transparent = False
scene.render.filepath = str(HERE / "ET011TJ1-preview.png")
scene.world.use_nodes = True
background = scene.world.node_tree.nodes.get("Background")
background.inputs["Color"].default_value = (0.055, 0.075, 0.105, 1.0)
background.inputs["Strength"].default_value = 1.0
scene.view_settings.look = "AgX - Medium High Contrast"
scene.view_settings.exposure = 1.0
bpy.ops.render.render(write_still=True)
