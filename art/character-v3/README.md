# Cloudsong character v3

AI-generated project artwork made with OpenAI's built-in image generation on 2026-11-08. Derived from the project's v2 character references, with a centered upright hood, undecorated cape back, and compact flat-soled boots. Front, back and side images were visually inspected before submission to Hyper3D. Integrated into the game; see preparation notes below.

## Front reference prompt

Edit this front-view character modeling reference with ONLY these corrections. Preserve this exact character, face, eye placement, proportions, A-pose, outfit, cape length, colors, composition and soft lighting. 1. The ivory hood's crown must be completely straight upright and centered over the midpoint of the face: a soft rounded pointed hood with bilateral symmetry, no sideways lean, hook, curl or swept tip. Keep its broad rounded lower volume and rolled face opening; do not make it a tall wizard cone. 2. Both brown boots must be soft compact flat-soled boots, no separate raised heel, no heel block, no protruding rear lip. Thin continuous flat sole flush with the upper, rear of foot close to the vertical calf line, rounded modest toes. 3. No diamond, emblem, badge or decorative shape anywhere on the cape, either geometry or painted. Keep the round front neck clasp and thin gold cape trim. No new details. One full-body front orthographic view on light gray background, suitable as a 3D modeling reference.

## Back reference prompt

Edit the FIRST image (rear view) to match the corrected SECOND image (front view) exactly as two views of ONE character. Keep first image's back viewpoint, A-pose, proportions, framing, colors and cape drape. Corrections: Ivory hood has a completely straight upright centered rounded point, bilateral symmetry, no sideways lean, hook or curl. Its rear surface is smoothly convex with no hollow, depression, dimple or face-like contour. Fine straight center seam runs continuously from neck up to the centered tip. REMOVE the gold diamond entirely: uninterrupted plain coral cloth on the back, no emblem, patch, symbol, bump or painted mark. Preserve only the thin gold hem. Boots have thin continuous flat soles flush with uppers, no raised heels or rear protrusion, rounded compact soft rear closely following the calf line. Preserve the shallow rear cape split and two rounded tails. Match hood height and width of corrected front view. One isolated full-body rear orthographic image on light gray background. No face or eyes visible from behind, no labels, no extra views or scenery.

## Side reference prompt

Create a precisely matching SIDE orthographic modeling reference of the SAME character shown in these front and back references. One full-body character facing RIGHT, pure 90-degree side profile, neutral standing A-pose identical to references, same body/hood/cape dimensions, same light gray background and even matte studio lighting. Show real depth rather than a flat cutout. Critical: ivory hood is straight upright in ALL axes, centered over head, rounded pointed crown, no forward/back or sideways droop; back of hood smoothly convex with no hollow, crease or dimple. Face is a recessed pale oval inside hood, tiny dark eye, no projecting nose. Cape drapes down behind body, coral cloth with thin gold trim, completely undecorated back, NO diamond/badge/emblem. Preserve ivory tunic, gloves, leggings and the round gold clasp on FRONT neck. Most important boots: soft plain brown ankle/calf boots with short rounded toes pointing RIGHT, rear contour almost directly below calf, heel does NOT project backward. Very thin continuous flat sole flush with leather all around, no raised heel block, no heel step, no projecting lip, no thick sole, no western/riding boot shape. Legs straight, feet flat, no flying pose. Maintain distinct arms and cape with natural thickness; only one character, no text, panels, scenery or props.

## Model generation

Rodin Gen-2.5 High, Quad, 30,000 polygon target, GLB, texture de-lighting enabled.
Generation ID: `9b35bbd4-0ba1-48d4-84b0-6121d29eebb1`.

Completed. Inspected the actual textured model in Hyper3D from front, side and rear: centered upright hood,
smooth convex rear without the prior hollow, plain cape back without diamond, compact boots without the prior heel block.

## Rigging

Rigging generation ID: `6586562f-d4df-4517-b695-4fb076d6afa4`. Custom skeleton, shell skinning, GLB, no animation clips.
Completed. Rigged result: https://hyper3d.ai/workspace/rodin/6586562f-d4df-4517-b695-4fb076d6afa4
Static source: https://hyper3d.ai/workspace/rodin/9b35bbd4-0ba1-48d4-84b0-6121d29eebb1

Verified the live Skeleton editor's bone tree: hips/spine/chest/neck/head, both arms/hands/fingers and
legs/feet/toes, `Hood1` through `Hood3`, `LeftCapeEdge1..3`, `RightCapeEdge1..3`,
`LeftCapeRear1..3` and `RightCapeRear1..3`. This confirms 3 hood bones and 12 cape bones were created.
Skinning job completed. The PBR GLB is preserved in `export/hero-rigged.glb`; the prepared version is now
integrated into Cloudsong. No authored animation clips were supplied: the game retargets its procedural gaits.

Requested skeleton:

Full-body biped humanoid in neutral A-pose. Root at pelvis; hips, 3 spine bones, chest, neck, head. Two arms each with clavicle, upper arm, forearm, wrist/hand, mitten fingers and thumb. Two legs each with thigh, shin, ankle/foot and toe. Add a 3-bone hood chain parented to head, from crown through middle to the upright cloth tip, for bending only the hood crown; keep face and lower hood on head. Add independent cape chains parented to chest/shoulders: 3 bones along left edge, 3 along right edge, 3 down left rear panel, 3 down right rear panel, ending at respective hem tails. Cape chains deform cloth, not arms or legs. Preserve original A-pose and centered straight hood as bind pose; do not tilt or pose it. Smooth skin weights for whole model, rigid boots to foot with ankle transition. No facial rig required.

ONE character shown in 3 references: FRONT, BACK, SIDE facing right. Match outfit, proportions and upright symmetric A-pose. Ivory fabric hood has a STRAIGHT CENTERED UPRIGHT rounded point, symmetric in front and rear, no lean, bend or curl. Smoothly convex back of hood, NO hollow or dimple; fine center seam continues to centered tip. Face: recessed ivory oval with two small dark eyes, no nose or mouth. Coral cape open at front and draping down behind body, thin gold trim, shallow split rear hem. Back is PLAIN CORAL CLOTH: absolutely NO diamond, emblem, badge, patch or painted symbol. Keep round gold clasp only at FRONT neck. Ivory tunic, gloves and leggings. Compact soft brown boots with THIN CONTINUOUS FLAT soles, no separate heel block, no heel step, no rear protrusion; back aligned closely under calf as side reference. Preserve clear separation of arms, legs and cape, smooth cloth folds, matte materials. No scenery, base, extra limbs, flying pose or added ornaments. Suitable for full-body rigging.


## Game preparation and integration

Run `python tools/prepare_hero.py` in an environment with NumPy and Pillow to reproduce `prepared/hero.glb` and
`src/HeroData.h`, then format the header with `xcrun clang-format -i src/HeroData.h`. Normal builds consume the
prepared GLB directly and do not require these Python libraries. Quench cooks the mesh and embedded PBR maps.

Normalized height: 1.12 m. Body: 32,170 vertices, 57,130 triangles, 68 skin joints. Retained the strongest four
influences across all four exported weight sets, then renormalized. The original first set alone held as little
as 50.4% of a vertex's weight. The exported hood joints were pointing down into the torso: the preparation moves
them to the crown and rebinds that region; the face and lower hood stay on Head. Central tunic arm/hand weights
are transferred to the torso. Stray hood/leg/arm influences on the cape are replaced by four symmetric cloth
chains. Boot shafts and heels stay rigid with the shin; only the forefoot bends about the repositioned ToeBase joint.
The knee rest positions have a stable forward bending plane. Cape/body weights blend across a narrow
mesh-neighbour band; coincident UV seam vertices receive identical weights, without cutting the welded mesh. A-pose arm corrections keep modest clearance from the tunic while preserving limb lengths.

The runtime keeps finger inheritance, retargets body poses, uses a fixed forward knee plane for the imported
legs and preserves boot heading during ground placement. This avoids the half-turn observed with the original
nearly straight imported legs. The visible forefoot follows the ground normal with a 45-degree flex limit;
the boot shaft is not curved into a second joint. It bends cape/hood
chains independently. Energy diamonds are separate small skinned surfaces, projected onto the actual cape;
they can be hidden/dimmed/lit as energy changes. Their halos and the trails follow skin-weighted anchors.
See the devlog for actual test, visual and performance results and unverified platforms.

Original export SHA-256: `e965e2fe9a773c4af4b05098f0d840d919d039489ab45e6b1627e28e39167ff4`.
Prepared source SHA-256: `82ec85b854eeb98f1a999268076dffb660b3927784e2819dccd726ebd8aa920c`.


## Stock animation audition

On 2026-11-08, after the user rejected the stiff procedural gait, created a separate Rodin rigging generation
from the same confirmed static model, using the standard `human` skeleton and shell skinning. Source model and
the game's repaired 68-bone skin were left intact.

Result: https://hyper3d.ai/workspace/rodin/70e1e47f-a119-4160-91df-ae253f988200
Requested GLB, `in_place=true`, seven stock clips:
`human_sp/idle`, `human_sp/happy_idle`, `human_sp/walking`, `human_sp/jogging`,
`human_sp/run`, `human_sp/run_2`, `human_sp/jumping`.
The PBR export is preserved at `animations/hero-stock.glb`: seven embedded clips on a standard 52-joint human
rig. `tools/prepare_hero_motion.py` only ships Idle, Walking and Jogging, closes loop seams and trims the
three-stride jog to one stride. Its `.anim.json` supplies foot-contact sound events. Run2 is rejected: the
source's face points 56–66 degrees sideways through the cycle. Jumping is an obstacle-clearing motion, not an
ordinary hop; removing its four-metre displacement or root bank does not change that action. Both clips remain
in the untouched export for provenance, but are excluded from the prepared clip set.

`animations/retarget.json` maps body joints onto the repaired 68-joint model, with explicit size conversion;
Chest receives the standard rig's Spine2, and the additional spine/garment joints inherit. Quench cooks the
skeleton and retargeted clips; `assets/hero.graph.json` cross-fades idle and walk/jog, with cadence following speed.
The controller remains authoritative for travel. Ordinary jumps gather both legs during ascent and extend them
on descent according to actual vertical velocity, rather than playing a fixed vault. Jump/flight/call/sitting
blend in joint-local space with gameplay poses. Hood motion uses a damped spring driven by speed, acceleration, turning and wind, only above the stable
face/rim. Grounded legs use independent height queries, a small stance correction on slopes, and triangle normals for
forefoot flex. Shaft and heel remain rigid, preserving the boot silhouette. Toe flex is limited to 45 degrees.
Verification results are recorded separately in the devlog.

In the live Skeleton/Animation viewer, selected Jogging and enabled In Place (the viewer's own checkbox initially
remained off despite the export request). Inspected poses at 0, 0.35 and approximately 0.77 seconds, then left the
preview playing. The sampled jogging poses show bent elbows and alternating knees without the prior reversed
boot. This is a stock-motion preview, not verification of every clip or cloth collisions. The standard human rig
has no custom cape/hood chains: the game's garment rig must be retained separately when transferring body motion.

Stock motion export SHA-256: `86aa3302e6ec1b4cf8e08edc99dacad5d216d086544dab587973219fbd5d5fbb`.
