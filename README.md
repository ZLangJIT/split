# Split
a directory / file splitter designed to split a file or a directory into fixed sized chunks

# compiler preprocessor defines

```sh
# list all macro's predefined by gcc
gcc -dM -E - < /dev/null | sort

# list all macro's predefined by g++
g++ -dM -E - < /dev/null | sort

# list all macro's predefined by clang
clang -dM -E - < /dev/null | sort

# list all macro's predefined by clang++
clang++ -dM -E - < /dev/null | sort
```

# Building

## Windows (win32,win64)
```sh
# replace C:/msys2 with the path to your MSYS2 installation
C:/msys2/msys2_shell.cmd -defterm -here -no-start -msys2 -shell bash -c "pacman -S --noconfirm --needed cmake ninja gcc ; ./make.sh"
```

## Unix (linux,macos,android)
```sh
./make.sh"
```

# Usage

```
$ ./build/split.exe

--split  [-n] [-r] [--size <split_size>] [--name <name>] <dir/file>
         info
                 split a directory/file into fixed size chunks
                 symlinks WILL NOT be followed
                 the split files will be created in the current working directory
         -n
                 form the split map only, does not store any content
         -r
                 remove each directory/file upon being stored
                 if -n is given, no directories/files will be removed
         --size
                 specifies the split size, the default is 4 MB
                 if a value of zero is specified then the default of 4 MB is used
                 if this options is not specified then the default of 4 MB is used
         --name
                 specifies the prefix to be added to split.* files
                 if this options is not specified then an empty prefix is used
         <dir/file>
                 directory/file to split

--join   [-n] [-r] [[prefix.]split.map | [http|https|ftp|ftps]://URL ] --out <out_dir>
         info
                 join a split map to restore a directory/file
         -n
                 list what would have been done, do not create/modify anything
                 if a URL is given, then only the *split.map is downloaded
                 if a URL is given, then the downloaded *split.map is automatically
                 removed as-if [-r] where given
         -r
                 remove each *split.* upon its contents being extracted
                 if -n is given, no *split.* files will be removed unless a URL was given
         [prefix.]
                 an optional prefix for the split map
         [http|https|ftp|ftps]://URL
                 a URL to a split.map file hosted online, local path rules apply
                   both http and https are accepted
                   both ftp and ftps are accepted
                   files CANNOT be split against http* and ftp*
                   http://url/[prefix.]split.map // usually redirects to https
                   https://url/[prefix.]split.map // redirected from http
                   https://url/[prefix.]split.0
                   https://url/[prefix.]split.1
                   https://url/[prefix.]split.2
                   ftp://url/[prefix.]split.2 // redirected
                   // https://url/[prefix.]split.2 -> ftp://url/[prefix.]split.2
                   https://url/[prefix.]split.3
                   and so on
                   NOTE: the above http/ftp mix cannot occur UNLESS the URL redirects to such
                   NOTE: the above http -> ftp redirect does not occur normally and requires
                         specific support from the web host/page
         --out
                 the directory to restore a directory/file into
                 defaults to the current directory

--ls     [[prefix.]split.map | [http|https|ftp|ftps]://URL ]
         info
                 list the contents of a split map
         [prefix.]
                 an optional prefix for the split map
         [http|https|ftp|ftps]://URL
                 a URL to a split.map file hosted online, local path rules apply
                 downloaded files are automatically removed as-if [ --join -n ] where used
                   both http and https are accepted
                   both ftp and ftps are accepted
                   files CANNOT be split against http* and ftp*
                   http://url/[prefix.]split.map // usually redirects to https
                   https://url/[prefix.]split.map // redirected from http
                   https://url/[prefix.]split.0
                   https://url/[prefix.]split.1
                   https://url/[prefix.]split.2
                   ftp://url/[prefix.]split.2 // redirected
                   // https://url/[prefix.]split.2 -> ftp://url/[prefix.]split.2
                   https://url/[prefix.]split.3
                   and so on
                   NOTE: the above http/ftp mix cannot occur UNLESS the URL redirects to such
                   NOTE: the above http -> ftp redirect does not occur normally and requires
                         specific support from the web host/page
```