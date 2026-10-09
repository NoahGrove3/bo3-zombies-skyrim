How the station spots in sheets/stations.json were found (run where Skyrim.esm is):
1. python3 esmscan.py Skyrim.esm refs.json   - references (base EDID, position) in the city worlds and Tamriel
2. python3 navscan.py Skyrim.esm nav.json    - navmesh triangles of the city worlds and round the five towns
3. python3 spots.py 0.8                       - for each place, spots where the stand (scaled 0.8) and its
   approach lie on walkable ground within 60 units of level, at least 300 from any door, nearest the hub
   (the market / town centre); writes spots_0.8.json
