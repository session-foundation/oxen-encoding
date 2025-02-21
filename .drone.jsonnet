local docker_base = 'registry.oxen.rocks/';

local submodule_commands = ['git fetch --tags', 'git submodule update --init --recursive --depth=1'];

local submodules = {
  name: 'submodules',
  image: 'drone/git',
  commands: submodule_commands,
};

local apt_get_quiet = 'apt-get -o=Dpkg::Use-Pty=0 -q ';

local kitware_repo(distro) = [
  'eatmydata ' + apt_get_quiet + ' install -y curl ca-certificates',
  'curl -sSL https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - >/usr/share/keyrings/kitware-archive-keyring.gpg',
  'echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ ' + distro + ' main" >/etc/apt/sources.list.d/kitware.list',
  'eatmydata ' + apt_get_quiet + ' update',
];

local debian_pipeline(name, image, arch='amd64', deps=['g++'], extra_setup=[], cmake_extra='', build_type='Release', extra_cmds=[], allow_fail=false) = {
  kind: 'pipeline',
  type: 'docker',
  name: name,
  platform: { arch: arch },
  environment: { CLICOLOR_FORCE: '1' },  // Lets color through ninja (1.9+)
  steps: [
    submodules,
    {
      name: 'build',
      image: image,
      pull: 'always',
      [if allow_fail then 'failure']: 'ignore',
      commands: [
                  'echo "Building on ${DRONE_STAGE_MACHINE}"',
                  'echo "man-db man-db/auto-update boolean false" | debconf-set-selections',
                  apt_get_quiet + 'update',
                  apt_get_quiet + 'install -y eatmydata',
                ] + extra_setup
                + [
                  'eatmydata ' + apt_get_quiet + 'dist-upgrade -y',
                  'eatmydata ' + apt_get_quiet + 'install -y cmake git ninja-build pkg-config ccache ' + std.join(' ', deps),
                  'mkdir build',
                  'cd build',
                  'cmake .. -G Ninja -DCMAKE_CXX_FLAGS=-fdiagnostics-color=always -DCMAKE_BUILD_TYPE=' + build_type + ' -DCMAKE_CXX_COMPILER_LAUNCHER=ccache ' + cmake_extra,
                  'ninja -v',
                  './tests/tests --use-colour yes',
                ] + extra_cmds,
    },
  ],
};

local clang(version) = debian_pipeline(
  'Debian sid/clang-' + version + ' (amd64)',
  docker_base + 'debian-sid-clang',
  deps=['clang-' + version],
  cmake_extra='-DCMAKE_C_COMPILER=clang-' + version + ' -DCMAKE_CXX_COMPILER=clang++-' + version + ' '
);

local full_llvm(version) = debian_pipeline(
  'Debian sid/llvm-' + version + ' (amd64)',
  docker_base + 'debian-sid-clang',
  deps=['clang-' + version, 'lld-' + version, 'libc++-' + version + '-dev', 'libc++abi-' + version + '-dev'],
  cmake_extra='-DCMAKE_C_COMPILER=clang-' + version +
              ' -DCMAKE_CXX_COMPILER=clang++-' + version +
              ' -DCMAKE_CXX_FLAGS=-stdlib=libc++ ' +
              std.join(' ', [
                '-DCMAKE_' + type + '_LINKER_FLAGS=-fuse-ld=lld-' + version
                for type in ['EXE', 'MODULE', 'SHARED', 'STATIC']
              ])
);

local macos_pipeline(name, arch, build_type) = {
  kind: 'pipeline',
  type: 'exec',
  name: name,
  platform: { os: 'darwin', arch: arch },
  environment: { CLICOLOR_FORCE: '1' },  // Lets color through ninja (1.9+)
  steps: [
    { name: 'submodules', commands: submodule_commands },
    {
      name: 'build',
      commands: [
        'mkdir build',
        'cd build',
        'cmake .. -G Ninja -DCMAKE_CXX_FLAGS=-fcolor-diagnostics -DCMAKE_BUILD_TYPE=' + build_type + ' -DCMAKE_CXX_COMPILER_LAUNCHER=ccache',
        'ninja -v',
        './tests/tests --use-colour yes',
      ],
    },
  ],
};


[
  {
    name: 'lint check',
    kind: 'pipeline',
    type: 'docker',
    steps: [{
      name: 'build',
      image: docker_base + 'lint',
      pull: 'always',
      commands: [
        'echo "Building on ${DRONE_STAGE_MACHINE}"',
        apt_get_quiet + ' update',
        apt_get_quiet + ' install -y eatmydata',
        'eatmydata ' + apt_get_quiet + ' install --no-install-recommends -y git clang-format-16 jsonnet',
        './utils/ci/lint-check.sh',
      ],
    }],
  },

  debian_pipeline('Debian sid (amd64)', docker_base + 'debian-sid'),
  debian_pipeline('Debian sid/Debug (amd64)', docker_base + 'debian-sid', build_type='Debug'),
  clang(17),
  full_llvm(17),
  clang(19),
  full_llvm(19),
  debian_pipeline('Debian bullseye (amd64)', docker_base + 'debian-bullseye'),
  debian_pipeline('Debian stable (i386)', docker_base + 'debian-stable/i386'),
  debian_pipeline('Debian sid (ARM64)', docker_base + 'debian-sid', arch='arm64'),
  debian_pipeline('Debian stable (armhf)', docker_base + 'debian-stable/arm32v7', arch='arm64', cmake_extra='-DCMAKE_CXX_FLAGS=-Wno-error=maybe-uninitialized'),
  debian_pipeline('Debian bullseye (armhf)', docker_base + 'debian-bullseye/arm32v7', arch='arm64'),
  debian_pipeline('Ubuntu noble (amd64)', docker_base + 'ubuntu-noble'),
  debian_pipeline('Ubuntu jammy (amd64)', docker_base + 'ubuntu-jammy'),
  debian_pipeline('Ubuntu focal (amd64)',
                  docker_base + 'ubuntu-focal',
                  deps=['g++-10'],
                  cmake_extra='-DCMAKE_C_COMPILER=gcc-10 -DCMAKE_CXX_COMPILER=g++-10'),
  macos_pipeline('macOS (Release, Intel)', 'amd64', 'Release'),
  macos_pipeline('macOS (Debug, Intel)', 'amd64', 'Debug'),
  macos_pipeline('macOS (Release, ARM)', 'arm64', 'Release'),
  macos_pipeline('macOS (Debug, ARM)', 'arm64', 'Debug'),
  {
    kind: 'pipeline',
    type: 'docker',
    name: 'Windows (amd64)',
    platform: { arch: 'amd64' },
    steps: [
      submodules,
      {
        name: 'build',
        image: docker_base + 'debian-win32-cross',
        pull: 'always',
        commands: [
          'echo "Building on ${DRONE_STAGE_MACHINE}"',
          'echo "man-db man-db/auto-update boolean false" | debconf-set-selections',
          apt_get_quiet + ' update',
          apt_get_quiet + ' install -y eatmydata',
          'eatmydata ' + apt_get_quiet + ' install --no-install-recommends -y build-essential cmake ninja-build ccache g++-mingw-w64-x86-64-posix wine64',
          'update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix',
          'update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix',
          'mkdir build',
          'cd build',
          'cmake .. -G Ninja -DCMAKE_CXX_COMPILER=/usr/bin/x86_64-w64-mingw32-g++-posix -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_CXX_COMPILER_LAUNCHER=ccache',
          'ninja -v',
          'export WINEPATH="$$(dirname $$(/usr/bin/x86_64-w64-mingw32-g++-posix -print-libgcc-file-name));/usr/x86_64-w64-mingw32/lib"',
          'WINEDEBUG=-all wine-stable ./tests/tests.exe --use-colour yes',
        ],
      },
    ],
  },
]
