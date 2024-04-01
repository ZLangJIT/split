export MSYS=winsymlinks:native

./make.sh || exit
/bin/cp -f ./build/split.exe . || exit

rm -rf sym
rm -rf sym2
rm -rf sym3
rm -rf sym4
mkdir sym
cp split_main.cpp sym/b
ln -s ../sym/b sym/a
ln -s d sym/c
./split.exe --split -r sym --name sym || exit
./split.exe --join ./sym.split.map --out sym -r || exit
rm -rfv sym.split*
./split.exe --split -r sym/a --name sym || exit
./split.exe --join ./sym.split.map --out sym2 -r || exit
rm -rfv sym.split*
./split.exe --split -r sym/b --name sym || exit
./split.exe --join ./sym.split.map --out sym3 -r || exit
rm -rfv sym.split*
./split.exe --split -r sym/c --name sym || exit
./split.exe --join ./sym.split.map --out sym4 -r || exit
rm -rfv sym.split*
ls -l sym*
rm -rf sym
rm -rf sym2
rm -rf sym3
rm -rf sym4
