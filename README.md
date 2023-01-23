DOSVAXJ3
====

DOSbox の AX, J-3100, DOS/V 対応版です。  
akm 氏による DOSbox の派生版 DOSVAX 4000AX11 をベースとし、Wengier Wu 氏による DOSBox-lfn の修正を取り込んでいます。  
SDL は IME 対応の [SDL-IM-plus](https://github.com/nanshiki/SDL-IM-plus) を使用してください。  
実際に使用している jp.kl のソースは key_jp の jp.key です。  

DOSVAXJ3 [README](https://github.com/nanshiki/DOSVAXJ3/blob/master/README.txt)　[技術情報](https://github.com/nanshiki/DOSVAXJ3/blob/master/Knowledge.txt)  

DOSBox [README](https://github.com/nanshiki/DOSVAXJ3/blob/master/README_DOSBox.txt)  

## ビルド
### Windows  
Visual Studio の 2015 以降で visualc_net/dosbox.sln を読み込み、ビルドしてください。  
SDL_net, zlib, libpng, pdcurses 等のライブラリをビルドし、リンクできるようにしておいてください。  

### Linux  
$ ./autogen.sh  
$ ./configure  
$ make  
$ sudo make install  
SDL-IM-plus をビルド、インストールしておく必要があります。  
Debian 系の場合、automake libsdl-sound1.2 libsdl-net1.2 あたりをインストールしておいてください。  
東雲フォントを内蔵したため日本語の 8x16, 8x19, 16x16 ドットフォントについては Xorg のフォントのインストールは不要です。  
CentOS、Fedora の場合 xorg-x11-fonts-misc、Raspberry Pi の場合 xfonts-base をインストールすると Xorg のフォントを使用します。  
日本語の 12x24, 24x24 ドットフォントについては Xorg フォントのインストールもしくは別途 FONTX2 のフォントファイルが必要です。
実行ファイル名は dosboxj、設定ファイルは ~/.dosboxj/dosboxj.conf です。

### macOS  
$ ./autogen.sh  
$ ./configure LIBS="-liconv"
$ make  
$ sudo make install  
SDL-IM-plus をビルド、インストールしておく必要があります。  
Homebrew で libiconv をインストールしておいてください。  
東雲フォントを内蔵したため日本語の 8x16, 8x19, 16x16 ドットフォントについては不要です。  
日本語の 12x24, 24x24 ドットフォントについては別途 FONTX2 のフォントファイルが必要です。  
実行ファイル名は dosboxj、設定ファイルは ~/Library/Preferences/dosboxj.conf です。  

## ライセンス
GPL v2
