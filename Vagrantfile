Vagrant.configure("2") do |config|
  config.vm.box = "debian/stretch64"
  config.vm.hostname = "wayproblems"
  config.vm.network :private_network, ip: "192.168.143.45"

  config.vm.provider "virtualbox" do |vb|
    vb.memory = "4096"
    vb.name = "wayproblems"
    vb.customize [ "modifyvm", :id, "--uartmode1", "disconnected" ]
  end

  config.vm.provision "shell", inline: <<-END
apt-get -fuy install build-essential cmake libboost-dev git libgdal-dev libbz2-dev libexpat1-dev libsparsehash-dev libboost-program-options-dev libgeos++-dev libproj-dev
cd /tmp

[ -d wayproblems ] || git clone git://pax.zz.de/wayproblems
[ -d libosmium ] || git clone https://github.com/osmcode/libosmium.git
[ -d protozero ] || git clone https://github.com/mapbox/protozero

cd wayproblems
cmake .
make
END
end
