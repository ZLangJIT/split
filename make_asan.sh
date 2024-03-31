export ACLOCAL_PATH=/usr/share/aclocal
cmake -G "Unix Makefiles" -DCMAKE_VERBOSE_MAKEFILE=off -DCMAKE_INSTALL_PREFIX=build_asan/root_fs -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DCMAKE_COLOR_DIAGNOSTICS=ON -DCMAKE_COLOR_MAKEFILE=ON -DCMAKE_C_FLAGS="-g3 -O0 -fno-omit-frame-pointer -fsanitize=address -fsanitize-address-use-after-scope -fno-common" -DCMAKE_CXX_FLAGS="-g3 -O0 -fno-omit-frame-pointer -fsanitize=address -fsanitize-address-use-after-scope -fno-common" -S . -B build_asan &&
cmake --build build_asan &&
cmake --install build_asan &&
echo "before running an asan build, it is recommended to execute the following in a shell" &&
echo "export ASAN_OPTIONS=verbosity=1:detect_leaks=0:detect_stack_use_after_return=1:check_initialization_order=true:strict_init_order=true:debug=1"
