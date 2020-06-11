# Building a Windows version on Linux

1. x86_64-w64-mingw32
```
    sudo apt-get install mingw-w64
```   
2. termcap
```
    wget https://ftp.gnu.org/gnu/termcap/termcap-1.3.1.tar.gz
    tar zxfv termcap-1.3.1.tar.gz
    cd termcap-1.3.1
    ./configure  --host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32
    sudo make install
```    
3. ncurses
```
    wget https://ftp.gnu.org/pub/gnu/ncurses/ncurses-6.2.tar.gz
    tar zxfv ncurses-6.2.tar.gz
    cd ncurses-6.2
    ./configure --host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32 --enable-term-driver --enable-sp-funcs
    sudo make install
```
4. libiconv
```
   wget https://ftp.gnu.org/gnu/libiconv/libiconv-1.16.tar.gz
   tar zxfv libiconv-1.16.tar.gz
   cd libiconv-1.16   
   ./configure --host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32
   sudo make install
```
5. readline
```
   wget https://ftp.gnu.org/gnu/readline/readline-8.0.tar.gz
   tar zxfv readline-8.0.tar.gz
   cd readline-8.0
   ./configure --host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32
   sudo make install
```
6. squirt
```
   cd squirt
   mkdir support
   cp `find /usr/x86_64-w64-mingw32/ -name libiconv-2.dll` support/
   cp `find /usr/x86_64-w64-mingw32/ -name libreadline8.dll` support/
   make mingw
```
