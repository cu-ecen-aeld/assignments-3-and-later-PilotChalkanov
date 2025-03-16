#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # Build kernel
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
#    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
    make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

# Copy resulting files generated in step 1.c to outdir
echo "Adding the Image in outdir"
ls -al
cp "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" "${OUTDIR}/Image"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# Create necessary base directories
mkdir -p "${OUTDIR}/rootfs"
cd "${OUTDIR}/rootfs"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
        # Configure busybox
        make distclean
        make defconfig
else
    cd busybox
fi
#Make and install busybox
make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make -j4 CONFIG_PREFIX="${OUTDIR}/rootfs" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
cd "${OUTDIR}/rootfs"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

# Add library dependencies to rootfs
TCHAIN_SYS_ROOT=$(aarch64-none-linux-gnu-gcc  -print-sysroot)

cp "$TCHAIN_SYS_ROOT/lib/ld-linux-aarch64.so.1" "${OUTDIR}/rootfs/lib/"
cp "$TCHAIN_SYS_ROOT/lib64/libc.so.6" "${OUTDIR}/rootfs/lib64/"
cp "$TCHAIN_SYS_ROOT/lib64/libresolv.so.2" "${OUTDIR}/rootfs/lib64/"
cp "$TCHAIN_SYS_ROOT/lib64/libm.so.6" "${OUTDIR}/rootfs/lib64/"

# Make device nodes
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1

# Clean and build the writer utility
cd "${FINDER_APP_DIR}"
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# Copy the finder related scripts and executables to the /home directory
cp writer.sh finder.sh writer start-qemu-app.sh finder-test.sh autorun-qemu.sh "${OUTDIR}/rootfs/home/"
cp conf/username.txt "${OUTDIR}/rootfs/home/conf"
cp conf/assignment.txt "${OUTDIR}/rootfs/home/conf"

#Chown the root directory
sudo chown -R root:root "${OUTDIR}/rootfs"

# Create initramfs.cpio.gz
cd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root > "${OUTDIR}/initramfs.cpio"
cd "${OUTDIR}"
ls -al
gzip -f initramfs.cpio