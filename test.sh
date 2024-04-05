./make.sh || exit
/bin/cp -f ./build/split.exe . || exit
(
	mkdir build_map
	rm -rf build_map
	cd build_map
	../split.exe --split -r ../build --name build || exit
	../split.exe --join ./build.split.map --out ../build -r || exit
)
rm ../split.exe