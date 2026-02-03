# xiso-ftp

A tool to convert .iso game files for the original xbox (.xiso files, that use the XDVD File System) into folder and file hierarchies that can be read on Xbox or emulators,
and transfer them to a modded Xbox without having to write (and later delete) the extracted files to your local machine.

# Considerations

Game files will extract to a new subfolder of the destination under the same name (or a similar/truncated name without special characters) as the game, minus the .iso extension, and will overwrite anything in that folder that shares names with the game data. If this is an issue for any reason, rename your conflicting iso to something else or make a folder for just your games.

7z archive extraction will be slower and will take up a lot of ram depending on how large the given archive is. This is because it takes time and resources to map the archive's structure and extract the iso files into allocated memory, before attempting to even parse for the xdvd signature. However this way, you only need the compressed 7z file on your computer in order to get your playable games onto your Xbox, and no other files or folders will be written to the drive, saving you unnecessary disk wear. 7z archive extraction will also attempt to extract ALL iso images within the archive (one at a time).

Right now I don't think this works on Windows for local extraction at least because of mkdir() and because of how paths were handled, so you have to use Linux/WSL.

# Usage

If you wish to transfer your game, make sure your Xbox's FTP server is connected and running. This is enabled by default for the UnleashX dashboard. You will need to run the command with the `-f` flag and provide a location on your Xbox for the game to go. The games folder by default for me is located at `F:/games/` on UnleashX.

You will be prompted to enter the IP of the Xbox, and the username and password for the dashboard. If you are on UnleashX, the IP is on the bottom right, and the default
username and password are both "xbox."

```
./xiso-ftp -f "F:/games/" "ExampleGame.iso"
Enter your xbox's network IP address: 10.0.0.1
Enter your xbox's network username: xbox
Enter your xbox's network password: xbox
```

If you just wish to extract the files locally on your machine, run the command with the `-x` flag and the location for the extracted location. This will create a new folder called "ExampleGame" under the destination given.

```
./xiso-ftp -x . "ExampleGame.iso"
```

.7z archive files are now supported, and selecting one to be used by the program will attempt to extract ALL isos within the archive, one by one. Simply specify that you are using a .7z file with the `-a` flag. Remember to specify local extraction or ftp too.

```
./xiso-ftp -a -x . "ExampleGame.7z"
```

To batch transfer all the games in the directory to your Xbox, I included a bash script that does this. Just configure the values in the `mass_ftp_script.sh` to your own and then you can place all your .iso/.7z files under `/scripts/games/` and run it. Your .isos/.7z files will go to a new folder called `transferred_games` after xiso-ftp is run on them to prevent confusion.

# Flags

All flags must go before the iso file. Either `-f` or `-x` must be specified, and both cannot be done at the same time.

- `-f <remote files destination>` : FTP mode. Requires the location on the Xbox for the game folder's parent directory (e.x "F:/games/", and then a new folder will be created at "F:/games/ExampleGame"). Prompts the user for an IP, username and password after. Does NOT extract any files locally.

- `-x <local files destination>` : Local extraction mode. Requires a valid destination on the local machine, and will create a subfolder for the game files.

- `-a` : Use this for .7z archives containing .isos. Extracts ALL iso files within the archive. Must also specifiy `-x` or `-f`.

- `-h` : Shows example usage.

# To compile:

CMake is required.

```
mkdir build
cd build
cmake ..
cmake --build .
```

Now the application should be in your current working directory.

# Requirements

Libcurl is needed for this program: https://ec.haxx.se/install/linux.html

An Xbox dashboard with an FTP server is needed (or technically any FTP server over port 21), this is the Rocky5 Softmod that installs UnleashX by default: https://github.com/Rocky5/Xbox-Softmodding-Tool/tree/master

# Credits

XDVD File system source material: https://multimedia.cx/xdvdfs.html

# TODO

[ ] - Add multithreading to improve local extraction speed (likely does not matter much for ftp extraction because of Xbox's network throughput)