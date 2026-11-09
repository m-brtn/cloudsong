"""Complete the generated radial birch branches with a three-dimensional green canopy."""
import bpy,math,random,shutil
from mathutils import Vector
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'art/world/windgarden'
if not(root/'silver_birch-source.glb').exists():shutil.copy2(root/'silver_birch.glb',root/'silver_birch-source.glb')
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.gltf(filepath=str(root/'silver_birch-source.glb'))
leaves=[]
for name,col in [('Jade',(.075,.24,.13)),('Sea green',(.12,.32,.20)),('New green',(.24,.41,.22))]:
 m=bpy.data.materials.new(name);m.use_nodes=True;b=m.node_tree.nodes.get('Principled BSDF');b.inputs['Base Color'].default_value=(*col,1);b.inputs['Roughness'].default_value=.9;leaves.append(m)
random.seed(7402);verts=[];faces=[];materials=[]
# Six branch-end clusters with real depth, not planes facing one camera.
clusters=[((-.35,-.12,.38),(.32,.32,.29)),((.33,.16,.47),(.34,.32,.30)),((-.15,.32,.63),(.31,.27,.30)),((.10,-.30,.60),(.29,.30,.29)),((-.14,-.06,.85),(.32,.30,.30)),((.29,-.07,.78),(.29,.32,.27))]
for centre,radius in clusters:
 for i in range(72):
  u=random.uniform(-1,1);a=i*2.39996323;v=Vector((math.sqrt(1-u*u)*math.cos(a),math.sqrt(1-u*u)*math.sin(a),u));p=Vector(centre)+Vector(tuple(v[j]*radius[j] for j in range(3)))*random.uniform(.75,1)
  normal=(v+Vector((0,0,.7))).normalized();right=normal.cross(Vector((0,1,0))).normalized();up=right.cross(normal).normalized();L=random.uniform(.067,.10);W=L*.58;start=len(verts)
  for local in [(-L,0,0),(-.45*L,-W,0),(.35*L,-W,0),(L,0,0),(.35*L,W,0),(-.45*L,W,0),(0,0,.018)]:verts.append(p+up*local[0]+right*local[1]+normal*local[2])
  for j in range(6):faces.append((start+j,start+(j+1)%6,start+6));materials.append(random.randrange(3))
mesh=bpy.data.meshes.new('Radial birch foliage');mesh.from_pydata(verts,[],faces)
for m in leaves:mesh.materials.append(m)
for p,k in zip(mesh.polygons,materials):p.material_index=k;p.use_smooth=True
obj=bpy.data.objects.new('Radial birch foliage',mesh);bpy.context.collection.objects.link(obj)
for m in leaves:m.use_backface_culling=False
bpy.ops.object.select_all(action='SELECT');bpy.ops.export_scene.gltf(filepath=str(root/'silver_birch.glb'),export_format='GLB',use_selection=True,export_animations=False)
