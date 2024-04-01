./make.sh || exit
/bin/cp -f ./build/split.exe . || exit
rm -rf build_map
mkdir build_map
cd build_map
../split.exe --split -r ../build --name build || exit
../split.exe --join ./build.split.map --out ../build -r || exit
