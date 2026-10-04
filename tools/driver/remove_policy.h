/*
 * Partition-removal policy for ps2hdd-hdl.irx (apaRemove).
 *
 * Upstream (HDLGameInstaller ec37c81, apa-hdl/src/hdd_fio.c) refuses to
 * remove every partition whose name starts with "__". That also blocks
 * hidden HDL game partitions ("__.<ID>..<TITLE>"), so an installed game
 * could never be deleted from the console.
 *
 * New rule, for names starting with "__" only:
 *   - the third character must be '.'   (name check, before lookup)
 *   - the partition type must be HDL    (type check, after lookup)
 * System partitions (__mbr, __net, __system, __sysconf, __common, ...)
 * have no '.' at index 2 and stay protected; "__.linux.*" partitions
 * are not of type HDL and stay protected. Names not starting with "__"
 * behave exactly as before.
 *
 * Compiled into the driver by patches/apa-hdl/0001-allow-removing-hidden-hdl-games.patch
 * and, unchanged, into the host tests (test/host/test_policy.c).
 */
#ifndef APA_REMOVE_POLICY_H
#define APA_REMOVE_POLICY_H

#define APA_REMOVE_POLICY_TYPE_HDL 0x1337

static int apaRemoveNameAllowed(const char *id)
{
    if (id[0] == '_' && id[1] == '_')
        return id[2] == '.';
    return 1;
}

static int apaRemoveTypeAllowed(const char *id, unsigned int type)
{
    if (id[0] == '_' && id[1] == '_')
        return type == APA_REMOVE_POLICY_TYPE_HDL;
    return 1;
}

#endif
