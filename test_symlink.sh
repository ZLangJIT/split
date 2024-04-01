export MSYS=winsymlinks:native

./make.sh || exit
/bin/cp -f ./build/split.exe . || exit

rm -rf sym
mkdir sym
cp split_main.cpp sym/b
ln -s ../sym/b sym/a
ln -s d sym/c
./split.exe --split -r sym --name sym || exit
./split.exe --join ./sym.split.map --out sym -r || exit
