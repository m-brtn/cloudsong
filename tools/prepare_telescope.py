"""Blender: replace the bent generated optical tube with straight aligned optics."""
import bpy,math
from pathlib import Path
from mathutils import Vector, Matrix
root=Path(__file__).resolve().parents[1]/'art/world/windgarden'
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(root/'telescope-source.glb'))
objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
coords=[o.matrix_world@v.co for o in objects for v in o.data.vertices]
lo=Vector(tuple(min(v[i] for v in coords) for i in range(3)));hi=Vector(tuple(max(v[i] for v in coords) for i in range(3)))
for o in objects:
 for v in o.data.vertices:v.co=(o.matrix_world@v.co-Vector(((lo.x+hi.x)/2,(lo.y+hi.y)/2,lo.z)))*2.3/(hi.z-lo.z)
 o.parent=None
 o.matrix_world=Matrix.Identity(4)
 # Retain only the original sculpted pedestal; never retain part of the bent tube.
 import bmesh
 bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.delete(bm,geom=[v for v in bm.verts if v.co.z>.51],context='VERTS');bm.to_mesh(o.data);bm.free()
def mat(name,color,metal=0):
 m=bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1);p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=.55;return m
ivory=mat('Ivory enamel',(.72,.75,.68));gold=mat('Aged brass',(.48,.32,.12),.5);teal=mat('Patinated mount',(.10,.28,.31),.2);lens=mat('Recessed optical glass',(.015,.08,.11),.25)
def cylinder(name,center,axis,radius,length,material):
 bpy.ops.mesh.primitive_cylinder_add(vertices=64,radius=radius,depth=length,location=center);o=bpy.context.object;o.name=name;o.rotation_euler=Vector(axis).to_track_quat('Z','Y').to_euler();o.data.materials.append(material)
 bevel=o.modifiers.new('Machined rim','BEVEL');bevel.width=.012;bevel.segments=3;bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=bevel.name)
 for p in o.data.polygons:p.use_smooth=abs(p.normal.z)<.9
 return o
cylinder('Pedestal neck',(0,0,.59),(0,0,1),.15,.27,teal)
# A real fork carries two coaxial horizontal trunnions.
for x in [-.24,.24]:
 cylinder('Mount fork',(x,0,.83),(0,0,1),.065,.55,teal)
cylinder('Elevation axle',(0,0,1.05),(1,0,0),.10,.64,gold)
axis=Vector((0,math.cos(.48),math.sin(.48)));centre=Vector((0,0,1.07))
cylinder('Straight enamel optical tube',centre,axis,.195,1.65,ivory)
for t,r,d in [(-.82,.217,.10),(-.42,.207,.045),(.30,.207,.045),(.83,.235,.14)]:
 cylinder('Aligned brass collar',centre+axis*t,axis,r,d,gold)
cylinder('Recessed objective lens',centre+axis*.903,axis,.210,.006,lens)
cylinder('Eyepiece tube',centre-axis*.96,axis,.075,.22,teal)
cylinder('Eyepiece rim',centre-axis*1.075,axis,.085,.04,gold)
cylinder('Eyepiece lens',centre-axis*1.1,axis,.065,.006,lens)
cylinder('Focus knob',(.31,-.05,.94),(1,0,0),.09,.09,gold)
bpy.ops.object.select_all(action='SELECT');bpy.ops.export_scene.gltf(filepath=str(root/'telescope.glb'),export_format='GLB',use_selection=True,export_animations=False)
