"""Blender offline: open the generated house, line and furnish its playable room.
Source export remains untouched. Run Blender --background --python tools/prepare_cottage.py.
"""
import bpy, bmesh, math
from pathlib import Path
from mathutils import Vector, Matrix
root=Path(__file__).resolve().parents[1]/'art/world/windgarden'
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(root/'cottage-source.glb'))
objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
for o in objects:
 bpy.context.view_layer.objects.active=o;o.select_set(True)
 bpy.ops.object.transform_apply(location=False,rotation=True,scale=True);o.select_set(False)
coords=[o.matrix_world@v.co for o in objects for v in o.data.vertices]
low=Vector(tuple(min(v[i] for v in coords) for i in range(3)));high=Vector(tuple(max(v[i] for v in coords) for i in range(3)))
scale=6/(high.z-low.z);centre=Vector(((high.x+low.x)/2,(high.y+low.y)/2,low.z))
for o in objects:
 for v in o.data.vertices:v.co=(o.matrix_world@v.co-centre)*scale
 o.parent=None
 o.matrix_world=Matrix.Identity(4)
# Weld the export's UV seam duplicates before solid booleans. Open seam edges
# make Difference create erroneous exterior caps and invalidate interior winding.
for o in objects:
 bm=bmesh.new();bm.from_mesh(o.data)
 bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.0001)
 bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
 print('repaired shell',len(bm.verts),'boundary',sum(e.is_boundary for e in bm.edges),flush=True)
 bm.to_mesh(o.data);bm.free()
def material(name,color,rough=.85,emission=0):
 m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);m.use_nodes=True
 bs=m.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Roughness'].default_value=rough
 if emission:bs.inputs['Emission Color'].default_value=(*color,1);bs.inputs['Emission Strength'].default_value=emission
 return m
plaster=material('Warm lime plaster',(.59,.61,.52));wood=material('Walnut joinery',(.17,.085,.042));woodlight=material('Oak end grain',(.30,.18,.085));stone=material('Hearth limestone',(.32,.40,.40));teal=material('Woven sea-green cloth',(.08,.25,.25));brass=material('Aged brass',(.39,.28,.10),.5);ivory=material('Ceramic ivory',(.71,.68,.52));fire=material('Quiet ember',(.9,.25,.035),.9,1.4)
def cube(name,at,size,mat,bevel=0):
 bpy.ops.mesh.primitive_cube_add(size=1,location=at);o=bpy.context.object;o.name=name;o.dimensions=size;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(mat)
 if bevel:
  mod=o.modifiers.new('Soft handmade edges','BEVEL');mod.width=bevel;mod.segments=3;bpy.ops.object.modifier_apply(modifier=mod.name)
 return o
def sphere(name,at,size,mat):
 bpy.ops.mesh.primitive_uv_sphere_add(segments=16,ring_count=8,location=at);o=bpy.context.object;o.name=name;o.scale=size;o.data.materials.append(mat)
 for p in o.data.polygons:p.use_smooth=True
 return o
def cut(cutter):
 for o in objects:
  bpy.context.view_layer.objects.active=o
  mod=o.modifiers.new('Playable opening','BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cutter;mod.use_self=True;mod.use_hole_tolerant=True
  bpy.ops.object.modifier_apply(modifier=mod.name)
 bpy.data.objects.remove(cutter,do_unlink=True)
cut(cube('Room cavity',(0,.25,1.75),(5.05,4.15,2.52),plaster))
cut(cube('Door passage',(-1.35,-2.65,1.35),(1.5,3.3,1.78),plaster))
bpy.ops.mesh.primitive_cylinder_add(vertices=48,radius=.75,depth=3.3,location=(-1.35,-2.65,2.24),rotation=(math.pi/2,0,0));bpy.context.object.data.materials.append(plaster);cut(bpy.context.object)
cut(cube('Front window light',(1.38,-2.25,2.0),(1.35,1.6,1.15),plaster))
cut(cube('Side window light',(2.5,-.15,2.0),(1.5,1.1,1.15),plaster))
for o in objects:
 o.data.materials.append(plaster);idx=len(o.data.materials)-1
 for f in o.data.polygons:
  coords=[o.data.vertices[i].co for i in f.vertices]
  if any(all(abs(v[axis]-level)<.0001 for v in coords) for axis,level in [(0,-2.525),(0,2.525),(1,-1.825),(1,2.325),(2,3.01)]):
   f.material_index=idx
# The boolean opens the actual light wells, but removes the generated window's
# painted/relief crossbars. Rebuild oak reveals and glazing bars around the same
# opening so the exterior reads as a window, not a blank view of the back wall.
for x in [1.38-.735,1.38+.735]:cube('Front window jamb',(x,-2.48,2.0),(.12,.24,1.42),woodlight,.025)
for z in [1.365,2.635]:cube('Front window rail',(1.38,-2.48,z),(1.59,.24,.12),woodlight,.025)
cube('Front window mullion',(1.38,-2.50,2.0),(.065,.18,1.18),woodlight,.012)
cube('Front window transom',(1.38,-2.50,2.0),(1.36,.18,.065),woodlight,.012)
cube('Front window sill',(1.38,-2.55,1.325),(1.73,.42,.12),woodlight,.025)
for y in [-.15-.61,-.15+.61]:cube('Side window jamb',(2.52,y,2.0),(.24,.12,1.42),woodlight,.025)
for z in [1.365,2.635]:cube('Side window rail',(2.52,-.15,z),(.24,1.34,.12),woodlight,.025)
cube('Side window mullion',(2.54,-.15,2.0),(.18,.065,1.18),woodlight,.012)
cube('Side window transom',(2.54,-.15,2.0),(.18,1.11,.065),woodlight,.012)
# A continuous floor with narrow individual boards. The door threshold stays flush.
cube('Floor foundation',(0,.25,.45),(5.08,4.2,.10),wood)
for i in range(17):cube('Floor plank',(-2.38+i*.298,.25,.515),(.289,4.12,.03),woodlight if i%3 else wood,.009)
# Exposed ceiling rafters and trim; all clear of the child's camera and walk space.
for y in [-1.55,-.3,.95,2.15]:cube('Ceiling rafter',(0,y,2.91),(5.04,.12,.18),wood,.025)
for x in [-2.44,2.44]:cube('Room skirting',(x,.25,.67),(.13,4.1,.23),wood,.02)
# Fireplace against the rear wall, with real dark recess between its piers.
cube('Hearth slab',(-1.45,1.76,.60),(1.65,.95,.16),stone,.05)
for x in [-2.04,-.86]:cube('Fireplace jamb',(x,2.02,1.12),(.25,.42,1.03),stone,.07)
cube('Mantel',(-1.45,2.02,1.70),(1.65,.55,.22),stone,.06)
cube('Chimney breast',(-1.45,2.17,2.53),(1.28,.30,1.5),plaster,.08)
cube('Fire recess',(-1.45,2.29,1.13),(1.0,.04,.88),wood,.01)
for i in range(3):
 o=cube('Hearth log',(-1.75+.30*i,1.96,.75),(.48,.15,.14),wood,.06);o.rotation_euler.z=(-.3+.3*i)
 sphere('Quiet ember',(-1.7+.25*i,1.9,.83),(.10,.07,.08),fire)
# Low table at the right, so the door-to-fireplace route is unobstructed.
for x in [1.10,2.00]:
 for y in [-.3,.7]:cube('Table leg',(x,y,.83),(.11,.11,.64),wood,.025)
cube('Table top',(1.55,.2,1.18),(1.22,1.35,.12),woodlight,.06)
cube('Table runner',(1.55,.2,1.247),(.36,1.33,.012),teal,.005)
for x,y in [(1.25,-.1),(1.85,.3)]:
 sphere('Tea cup',(x,y,1.31),(.085,.085,.07),ivory)
 sphere('Saucer',(x,y,1.258),(.13,.13,.012),ivory)
sphere('Tea pot',(1.55,.65,1.4),(.14,.14,.15),teal)
sphere('Tea pot lid',(1.55,.65,1.54),(.10,.10,.025),brass)
# Bookcase and pottery on the left wall, scaled for the small inhabitants.
for x in [-2.42,-1.65]:cube('Shelf upright',(x,.0,1.3),(.10,.30,1.55),wood,.02)
for z in [.65,1.12,1.59,2.06]:cube('Bookshelf',(-2.04,0,z),(.9,.34,.07),woodlight,.02)
for i in range(6):
 mat=[teal,woodlight,ivory][i%3];o=cube('Unlettered book',(-2.32+i*.105,-.01,1.34),(.08,.23,.35-.03*(i%2)),mat,.009);o.rotation_euler.y=.04*(i%3-1)
for x in [-2.25,-1.92]:sphere('Ceramic jar',(x,0,1.77),(.10,.12,.16),teal if x< -2 else ivory)
# Reading bench and a woven rug; no chairs blocking the doorway.
for x in [.25,1.65]:cube('Bench foot',(x,1.85,.74),(.12,.30,.43),wood,.025)
cube('Window bench',(.95,1.85,.97),(1.75,.65,.16),woodlight,.04)
cube('Bench cushion',(.95,1.85,1.08),(1.65,.58,.12),teal,.06)
cube('Woven rug',(-.1,-.30,.54),(1.55,1.45,.015),teal,.03)
for x in [-.82,.62]:cube('Rug border',(x,-.3,.55),(.025,1.35,.008),ivory)
# glTF faces -Z in the game. Apply a single authored rotation to the whole room.
for o in list(bpy.context.scene.objects):
 if o.type=='MESH':
  o.matrix_world=Matrix.Rotation(math.pi,4,'Z')@o.matrix_world
bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.gltf(filepath=str(root/'cottage.glb'),export_format='GLB',use_selection=True,export_yup=True,export_animations=False)
print('Prepared a 6 m cottage, floor .53, entry x=+1.35,z=-2.65, with a hollow room and furniture.')
