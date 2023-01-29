set -e

if [ "$1" == "ubuntu-18.04" ]; then
        function sudo() {
                eval $@
        }
fi

function install-rgbds() {
        cd `mktemp -d`
        curl -L https://github.com/rednex/rgbds/archive/v0.6.0.zip > rgbds.zip
        unzip rgbds.zip
        cd rgbds-*
        make -sj
        sudo make install
        cd ..
        rm -rf *
}

case `echo $1 | cut -d '-' -f 1` in
        ubuntu)
                sudo apt-get -qq update
                sudo apt-get install -yq \
                        gcc clang \
                        bison \
                        libpng-dev \
                        pkg-config \
                        libsdl2-dev

                # needed by the libretro build system (hexdump)
                sudo apt-get install -yq bsdmainutils

                install-rgbds
                ;;
        macos)
                brew install rgbds sdl2
                ;;
        *)
                echo "Unsupported OS: $1"
                exit 1
                ;;
esac
