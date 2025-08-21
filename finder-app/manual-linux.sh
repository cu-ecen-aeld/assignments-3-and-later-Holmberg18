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


mkdir -p "${OUTDIR}"
cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} linux-stable --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e "${OUTDIR}/Image" ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # Deep clean the kernel build tree
    make O="${OUTDIR}" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper

    # Kernel configuration
    make O="${OUTDIR}" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig

    # Build the kernel image for booting with QEMU
    make -j4 O="${OUTDIR}" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all

    # Verify the image was created - CHECK THE CORRECT PATH!
    if [ ! -e "${OUTDIR}/arch/${ARCH}/boot/Image" ]; then
        echo "Kernel build failed - Image not found at ${OUTDIR}/arch/${ARCH}/boot/Image"
        exit 1
    fi

    # Move Image to ${OUTDIR}/Image
    mv "${OUTDIR}/arch/${ARCH}/boot/Image" "${OUTDIR}/Image"

    # Clean up arch directory
    rm -rf "${OUTDIR}/arch"

    echo "Kernel Image is at: ${OUTDIR}/Image"
fi

echo "Adding the Image in outdir"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
# Create rootfs directory structure
mkdir -p "${OUTDIR}/rootfs"
cd "${OUTDIR}/rootfs"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log
# Set permissions for tmp
chmod 1777 tmp

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone https://git.busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
else
    cd busybox
fi

# # TODO: Make and install busybox
echo "Building and caching BusyBox..."

# TODO: Make and install busybox
# Set the configuration non-interactively
make distclean
yes "" | make defconfig

make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX="${OUTDIR}/rootfs" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

# Verify the installation
if [ ! -f "${OUTDIR}/rootfs/bin/busybox" ]; then
    echo "BusyBox installation failed!"
    exit 1
fi

echo "Library dependencies:"
echo "=== COPYING REQUIRED LIBRARIES ==="

# Get the sysroot
echo "Getting sysroot..."
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot 2>/dev/null)

if [ -z "$SYSROOT" ] || [ ! -d "$SYSROOT" ]; then
    echo "ERROR: Cannot get sysroot from ${CROSS_COMPILE}gcc"
    exit 1
fi

echo "Using SYSROOT: $SYSROOT"

# Copy program interpreter (dynamic linker) to /lib
INTERPRETER=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter" | awk -F'[][]' '{print $2}' | awk -F'/' '{print $NF}')

cp -a "$SYSROOT/lib/$INTERPRETER" "${OUTDIR}/rootfs/lib/" 2>/dev/null || {
    echo "ERROR: Cannot find $INTERPRETER in sysroot"
    exit 1
}
echo "✓ Copied ${INTERPRETER} to /lib"

echo "Copying shared libraries to /lib64..."
mapfile -t LIBS < <(${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library" | awk -F'[][]' '{print $2}')

echo "LIBS array contains: ${#LIBS[@]} elements"
for LIB in "${LIBS[@]}"; do
    echo "Copying $LIB..."
    cp -a "$SYSROOT/lib64/$LIB" "${OUTDIR}/rootfs/lib64/" 2>/dev/null || {
        echo "ERROR: Cannot find $LIB in sysroot"
        exit 1
    }
    echo "✓ Copied $LIB to /lib64"
done


echo "Library copying completed!"

# TODO: Make device nodes
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 600 ${OUTDIR}/rootfs/dev/console c 5 1


# TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE}


# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp ${FINDER_APP_DIR}/writer ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/finder-test.sh ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/finder.sh ${OUTDIR}/rootfs/home/
cp ${FINDER_APP_DIR}/autorun-qemu.sh ${OUTDIR}/rootfs/home/

# Copy conf files to the correct location
mkdir -p ${OUTDIR}/rootfs/home/conf
cp ${FINDER_APP_DIR}/conf/{username,assignment}.txt ${OUTDIR}/rootfs/home/conf/

# Make scripts executable
chmod +x ${OUTDIR}/rootfs/home/writer
chmod +x ${OUTDIR}/rootfs/home/*.sh

sudo chown -R root:root ${OUTDIR}/rootfs

# TODO: Create initramfs.cpio.gz
echo "Creating initramfs from ${OUTDIR}/rootfs"
cd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio

# Verify the initramfs was created successfully
if [ ! -f "${OUTDIR}/initramfs.cpio.gz" ] || [ ! -s "${OUTDIR}/initramfs.cpio.gz" ]; then
    echo "ERROR: initramfs.cpio.gz was not created or is empty!"
    exit 1
fi

echo "Successfully created initramfs: ${OUTDIR}/initramfs.cpio.gz"
echo "File size: $(du -h ${OUTDIR}/initramfs.cpio.gz | cut -f1)"

# Test initramfs integrity
echo "Testing initramfs integrity..."
if ! zcat "${OUTDIR}/initramfs.cpio.gz" | cpio -itv > /dev/null 2>&1; then
    echo "ERROR: initramfs appears to be corrupt!"
    exit 1
fi

echo "initramfs validation passed - ready for QEMU boot"
echo "Build completed successfully!"