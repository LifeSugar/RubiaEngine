import bpy, json
from pathlib import Path
OUT=Path(__file__).resolve().parent
scene=bpy.context.scene
assert scene.name.startswith('Rubia Preparation Gallery')
# Valid identifiers inspected through get_format_items and RNA before use.
for i in range(10):
    for o in scene.objects: o.select_set(o.get('rubia_material_group')==i)
    bpy.ops.export_scene.gltf(filepath=str(OUT/f'group_{i:02d}.glb'),export_format='GLB',use_selection=True,use_active_scene=True,export_animations=False,export_normals=i!=0,export_texcoords=i>=2,export_tangents=i>=2,export_vertex_color='ACTIVE' if i==1 else 'NONE',export_materials='EXPORT')
for o in scene.objects: o.select_set(bool(o.get('rubia_environment')))
bpy.ops.export_scene.gltf(filepath=str(OUT/'environment.glb'),export_format='GLB',use_selection=True,use_active_scene=True,export_animations=False,export_tangents=True,export_vertex_color='ACTIVE')
for o in scene.objects: o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(OUT/'gallery.glb'),export_format='GLB',use_selection=True,use_active_scene=True,export_animations=False,export_tangents=True,export_vertex_color='ACTIVE')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'gallery.blend'))
print('Saved gallery.blend, gallery.glb and 10 groups')
