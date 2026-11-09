# Cloudsong region art

The region is authored in `src/World.cpp` and `src/Region.cpp`: seventeen floating isles, asymmetric terraced
ridges, sampled winding paths, short treads on steep parts, two walkable arched bridges, four bell towers,
two roofless arcades and one imported sanctuary. Stone groups, mossy garden markers and a pale cyan flower
meadow on the north-west isle give the side routes their own details. No geometry or textures were extracted
from the user's Sky references; those informed colour, composition, simplified vegetation and scale only.

The six new isles use seeds 101, 103, 107, 109, 113 and 127. Central journey destinations and progression are
preserved. The west bridge leads from the starting meadow toward the sanctuary; the east bridge leads from
the cave isle toward the bell gardens. Higher destinations are reached in flight.

The sanctuary's untouched Rodin export and provenance are in [sanctuary](sanctuary/CREDITS.md). It is cooked
with its PBR textures; render and collision use the same transformed vertices. Other region geometry is
original project code. The region check captures day/night views and checks bridge traversal and sanctuary
floor collision. This is the landscape/architecture pass, not a new quest campaign.
