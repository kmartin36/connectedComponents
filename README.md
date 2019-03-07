# connectedComponents
Single pass conected components algorithm

cc.cpp is more flexible but slower. Compile with:
g++ -std=c++11 -O3 -march=native -o cc cc.cpp

cc2.cpp only uses 8-connection labeling and uses ARM NEON intrinsics so will not run on other architectures, but runs much faster. Compile With:
g++ -std=c++11 -g -O3 -march=native -mfpu=neon -o cc2 cc2.cpp

Run either with:
./cc <input raw rgb image> <width> <height> <connection radius (assumed 1 by cc2)> <region size threshold> [list of color specifiers of the format: <name> <red minimum> <red maximum> <green minimum> <green maximum> <blue minimum> <blue maximum>]
Example:
./cc test.rgb 1920 1080 1 50 black 0 50 0 50 0 50 red 150 255 0 50 0 50 green 0 50 150 255 0 50 blue 0 50 0 50 150 255 yellow 150 255 150 255 0 50 purple 150 255 0 50 150 255 teal 0 50 150 255 150 255 none 255 0 255 0 255 0
