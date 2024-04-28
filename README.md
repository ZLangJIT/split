# Split
a directory / file splitter designed to split a file or a directory into fixed sized chunks

# features

## quick
- splits a file or directory into X sized chunks
- stores the `dictionary info` in a separate file
- can download an archive via a URL with plain-text unencrypted format
- CANNOT upload archives for numerous reasons

## full
- splits a file or directory into X sized chunks
    ```
    [a.txt b.txt c.txt] chunk 1
    [e.txt f.mp3 g.ts i.lib o.r] chunk 2
    and so on
    ```
  - as far as i know, most archival programs do not do this
    - the only ones i know that can are
      - WinRAR
        - (closed source)
      - 7Zip
        - (Open Source but not many api examples)
- stores the `dictionary infomation` in a separate file
  - this allows rapid listing of large archives by downloading only a small dictionary file that lists the content of the archive
  - this also allows efficient serialization of files since we need not include any metadata in the chunks
  - this also allows 1 byte sized chunks if desired
  - as far as i know, no archival program can do this
- can download an archive via a URL with plain-text unencrypted format
  ```
  https://.../split.map
  https://.../split.0
  ...
  ```
  - as far as i know, no archival program can do this
- CANNOT upload archives for numerous reasons
  - storage services usually have their own file upload api's and we cannot magically handle all of them

# TODO
- specific file(s)/directory(s) extraction
  - due to the split system, it is possible to scan the metadata map to compute which chunks are required to extract a specific file(s)/directory(s), and then only download those required chunks
    - this can drastically reduce download time since the entire archive would not need to be downloaded just to extract a single file
    - however, this depends heavily on the split size used to create the archive, for example, if we create an archive with a 1 GB split size, then it is very likely that the bandwidth saved will be minimal unless the total complete download size outweighs the size of a single split chunk
      - for example, if we split by 1 GB, into 20 separate split files, we get a max chunk size of 1 GB with a total size of 20 GB
      - assuming we have a file with a size of 1 GB or less, this would allow us to download a min of 1 GB instead of the entire 20 GB
      - however, if we have a file that is close to 20 GB then we must download all 20 chunks (20 GB), or however many chunks the large file takes up, in order to extract the file

### compiler preprocessor defines (ignore this)
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
         -v
                 list each item as it is processed
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
         -v
                 list each item as it is processed
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