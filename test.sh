./make.sh &&
/bin/cp -f ./build/split.exe . &&
rm -rf build_map &&
mkdir build_map &&
cd build_map &&
../split.exe --split -r ../build --name build &&
../split.exe --join ./build.split.map --out ../build -r
