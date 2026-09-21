# Execute through BlenderMCP with __file__ set to this path.
import bpy, math, random
from pathlib import Path
from mathutils import Euler
import io_scene_gltf2
OUT=Path(__file__).resolve().parent
OUT.mkdir(parents=True, exist_ok=True)
print('GLTF formats:',io_scene_gltf2.get_format_items(None,bpy.context))
# Inspect identifiers before assigning enums (Blender 4.5).
for typ,prop in [(bpy.types.Image,'file_format'),(bpy.types.Material,'surface_render_method'),(bpy.types.Modifier,'type'),(bpy.types.View3DShading,'type'),(bpy.types.View3DShading,'color_type')]:
    print(typ.__name__,prop,[i.identifier for i in typ.bl_rna.properties[prop].enum_items])
scene=bpy.data.scenes.new('Rubia Preparation Gallery - Atelier')
bpy.context.window.scene=scene
random.seed(47)
labels=['PORCELAIN','VERTEX / PIGMENT','JADE GLAZE','COBALT / TILE','BRUSHED COPPER','LATTICE / 0.30','LATTICE / 0.70','CYAN / GLASS','ROSE / GLASS','TWO-SIDED / SILK']
colors=[(.88,.55,.25,1),(.85,.85,.85,1),(.17,.73,.53,1),(.18,.38,.85,1),(.95,.48,.24,1),(.72,.58,.24,1),(.12,.64,.52,1),(.15,.74,.9,.22),(.92,.3,.5,.28),(.75,.28,.21,1)]
textures={}
# Original procedural images: woven glaze, fine grooves, ORM, gold inlay.
for kind in ['checker','normal','orm','emission']:
    im=bpy.data.images.new('Atelier_'+kind,width=512,height=512,alpha=True)
    if kind in ['normal','orm']: im.colorspace_settings.name='Non-Color'
    px=[]
    for y in range(512):
        for x in range(512):
            u,v=x/512,y/512
            wave=math.sin((u+v)*math.tau*4+0.7*math.sin(v*math.tau*3))
            line=math.exp(-((wave/.11)**2))
            grain=.015*math.sin(x*2.13+y*3.1)
            if kind=='checker':
                tone=.72+.2*wave+grain
                a=.5+.49*math.sin(u*math.tau*6)*math.sin(v*math.tau*6)
                rgba=(tone,tone*.96,tone*.86,a)
            elif kind=='normal':
                nx,ny=.16*math.sin(u*math.tau*24),.09*math.cos(v*math.tau*24)
                rgba=(nx*.5+.5,ny*.5+.5,math.sqrt(1-nx*nx-ny*ny)*.5+.5,1)
            elif kind=='orm': rgba=(.9,.22+.25*(wave*.5+.5),.55+.4*line,1)
            else: rgba=(line*.8,line*.34,line*.05,1)
            px.extend(rgba)
    im.pixels.foreach_set(px); im.filepath_raw=str(OUT/(kind+'.png')); im.file_format='PNG'; im.save(); im.pack()
    textures[kind]=im
mats=[]
for i,name in enumerate(labels):
    mat=bpy.data.materials.new(f'{i:02d} '+name);mat.use_nodes=True;mat.diffuse_color=colors[i]
    bs=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    bs.inputs['Base Color'].default_value=colors[i];bs.inputs['Roughness'].default_value=.24
    bs.inputs['Metallic'].default_value=.7 if i==4 else .05
    if i>=2 and i not in [7,8]:
        tex=mat.node_tree.nodes.new('ShaderNodeTexImage');tex.image=textures['checker']
        mat.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color'])
    if i in [7,8]:
        mat.surface_render_method='BLENDED';bs.inputs['Alpha'].default_value=colors[i][3]
        bs.inputs['Roughness'].default_value=.12
    mats.append(mat)
stage=[]
stageMat=bpy.data.materials.new('Atelier architecture');stageMat.use_nodes=True
bs=next(n for n in stageMat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
vertexNode=stageMat.node_tree.nodes.new('ShaderNodeVertexColor');vertexNode.layer_name='Color'
stageMat.node_tree.links.new(vertexNode.outputs['Color'],bs.inputs['Base Color'])
bs.inputs['Roughness'].default_value=.6

def tint(obj,color):
    obj.data.materials.clear();obj.data.materials.append(stageMat)
    attr=obj.data.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='POINT')
    for d in attr.data:d.color=color
    # Environment UV0.x is an explicit fixture surface tag: floor=0, label=.5, lit=1.
    for uv in list(obj.data.uv_layers):obj.data.uv_layers.remove(uv)
    uv=obj.data.uv_layers.new(name='SurfaceTag')
    for loop in uv.data:loop.uv=(color[3],0)
    obj.color=color;obj['rubia_environment']=True;stage.append(obj)
    return obj

def cube(name,loc,scale,color,bevel=0):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.name=name;o.scale=scale
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if bevel:
        m=o.modifiers.new('Soft edges','BEVEL');m.width=bevel;m.segments=3
        bpy.ops.object.modifier_apply(modifier=m.name)
    return tint(o,color)

def label(text,loc,size,color,rotation=(0,0,0)):
    bpy.ops.object.text_add(location=loc,rotation=rotation);o=bpy.context.object;o.name='Label '+text
    o.data.body=text;o.data.size=size;o.data.extrude=.001;o.data.resolution_u=6
    bpy.ops.object.convert(target='MESH');return tint(bpy.context.object,color)

# Huge background plane, plus a wall. Both are ordinary opaque GPU geometry.
bpy.ops.mesh.primitive_plane_add(size=120,location=(0,0,-.12));tint(bpy.context.object,(.59,.65,.66,0))
cube('Backdrop wall',(0,8.7,3.8),(33,.25,8),(.024,.048,.067,1),.12)
label('R U B I A',( -12.8,8.54,5.85),1.2,(.87,.72,.43,.5),(math.pi/2,0,0))
label('M A T E R I A L   A T E L I E R',(-12.65,8.53,4.9),.31,(.69,.8,.79,.5),(math.pi/2,0,0))
label('40 FORMS  /  10 SURFACES', (5.7,8.53,5.92),.27,(.65,.76,.75,.5),(math.pi/2,0,0))
label('RUBIA  -  GPU PREPARATION STUDIES',(5.7,8.53,5.32),.18,(.42,.58,.61,.5),(math.pi/2,0,0))
for x in [-14.5,14.5]: cube('Brass wall trim',(x,8.48,3.6),(.035,.04,6.8),(.65,.39,.14,.5))
# Repeating display islands, each with a hero object and three smaller studies.
for i in range(10):
    x=(i%5-2)*5.2;y=3.55 if i<5 else -3.55
    cube(f'Plinth {i:02d}',(x,y,.17),(4.65,5.25,.5),(.105,.15,.17,1),.12)
    cube(f'Inset {i:02d}',(x,y,.435),(4.43,5.03,.05),(.72,.73,.67,1),.025)
    cube(f'Index strip {i:02d}',(x,y-2.30,.48),(4.25,.035,.02),colors[i][:3]+(.5,))
    label(f'{i:02d}  /  '+labels[i],(x-2.05,y-2.07,.482),.18,(.025,.045,.054,.5))
    positions=[(x-.2,y-.50,1.69),(x-1.27,y+1.10,1.03),(x+.25,y+1.36,1.01),(x+1.45,y+.66,1.07)]
    # Glass hero is large; its colored edge and opaque interior make blending obvious.
    types=['sphere','torus','cube','cone'] if i in [7,8] else ['sphere','cube','torus','ico']
    if i in [1,3,5]:types=['monkey','sphere','cone','cylinder']
    if i==9:types=['torus','cone','sphere','plane']
    for j,(kind,pos) in enumerate(zip(types,positions)):
        if kind=='sphere':bpy.ops.mesh.primitive_uv_sphere_add(segments=48,ring_count=24,location=pos)
        elif kind=='ico':bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2,location=pos)
        elif kind=='cube':bpy.ops.mesh.primitive_cube_add(size=1.6,location=pos)
        elif kind=='cone':bpy.ops.mesh.primitive_cone_add(vertices=48,location=pos)
        elif kind=='cylinder':bpy.ops.mesh.primitive_cylinder_add(vertices=48,location=pos)
        elif kind=='torus':bpy.ops.mesh.primitive_torus_add(major_segments=64,minor_segments=16,location=pos)
        elif kind=='monkey':bpy.ops.mesh.primitive_monkey_add(location=pos)
        else:bpy.ops.mesh.primitive_plane_add(size=2,location=pos)
        o=bpy.context.object;o.name=f'G{i:02d}_{j}_{kind}';o['rubia_material_group']=i
        size=1.13 if j==0 else .51;o.scale=(size,size,size)
        o.rotation_euler=(0,0,-.12) if j==0 else (.25,.3,.2*j)
        if kind=='torus':o.rotation_euler=(math.radians(68),.1,.2)
        if kind=='plane':o.rotation_euler=(math.radians(140),.15,0)
        if kind=='cube':
            bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
            m=o.modifiers.new('Rounded corners','BEVEL');m.width=.12;m.segments=4;bpy.ops.object.modifier_apply(modifier=m.name)
        if kind=='monkey':
            m=o.modifiers.new('Sculpt smooth','SUBSURF');m.levels=1;bpy.ops.object.modifier_apply(modifier=m.name)
        m=o.modifiers.new('Export triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=m.name)
        o.data.materials.clear();o.data.materials.append(mats[i])
        for face in o.data.polygons:face.use_smooth=kind not in ['cube','ico','plane']
        if i<2:
            for uv in list(o.data.uv_layers):o.data.uv_layers.remove(uv)
        if i==1:
            a=o.data.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='POINT')
            for v,d in zip(o.data.vertices,a.data):
                t=.5+.5*math.sin(v.co.z*2.8+v.co.x)
                d.color=(.1+.7*t,.62-.25*t,.7-.35*t,1)
    # Geometric opacity references sitting inside and behind glass (not a composited image).
    if i in [7,8]:
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1,radius=.46,location=(x-.2,y-.50,1.69))
        tint(bpy.context.object,(.85,.43,.09,1))
        for k in range(5):
            cube('Glass reference stripe',(x-1.65+k*.8,y+2.15,1.45),(.2,.06,1.9),(.14,.25,.28,1),.03)
# Export architecture as one vertex-colored mesh/material, keeping draw counts small.
for o in bpy.context.selected_objects:o.select_set(False)
for o in stage:o.select_set(True)
bpy.context.view_layer.objects.active=stage[0];bpy.ops.object.join()
bpy.context.object.name='Atelier background, plinths and glass references'
for o in bpy.context.selected_objects:o.select_set(False)
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        area.spaces.active.shading.type='SOLID';area.spaces.active.shading.color_type='MATERIAL'
        area.spaces.active.region_3d.view_perspective='PERSP'
        area.spaces.active.overlay.show_overlays=False
        area.spaces.active.region_3d.view_location=(0,0,1.8)
        area.spaces.active.region_3d.view_distance=31
        area.spaces.active.region_3d.view_rotation=Euler((math.radians(48),0,0)).to_quaternion()
print('Atelier complete:',len(scene.objects),'objects; 40 specimens plus one architectural mesh')
