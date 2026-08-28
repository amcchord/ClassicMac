/*
 * Minimal resource templates for the classic Mac OS Installer script
 * resources emitted by generate-installer-patch.py.
 *
 * These layouts are the public Installer 4.x SDK formats.  Keeping the small
 * subset here makes the custom-CD build independent of a separately installed
 * copy of the old Universal Interfaces package.
 */

#define evenPaddedString \
    pstring;             \
    align word

#define OSType literal longint
#define rsrcID integer

#define packageFlags                                      \
    boolean doesntShowOnCustom, showsOnCustom;            \
    boolean notRemovable, removable;                      \
    boolean forceRestart, dontForceRestart;               \
    fill bit[13]

type 'inpk' {
    switch {
        case format0:
            key integer = 0;
            packageFlags;
            rsrcID;
            unsigned longint;
            evenPaddedString;
            unsigned integer = $$CountOf(partsList);
            wide array partsList {
                OSType;
                rsrcID;
            };
    };
};

#define fileSpecFlags                                     \
    boolean noSearchForFile, SearchForFile;               \
    boolean TypeCrNeedNotMatch, TypeCrMustMatch;           \
    fill bit[14]

type 'infs' {
    OSType;
    OSType;
    unsigned hex longint;
    fileSpecFlags;
    evenPaddedString;
};

#define targetFileSpecFlags                               \
    boolean noSearchForFile, SearchForFile;               \
    boolean TypeCrNeedNotMatch, TypeCrMustMatch;           \
    fill bit[14]

type 'intf' {
    switch {
        case format1:
            key integer = 1;
            targetFileSpecFlags;
            OSType;
            OSType;
            unsigned hex integer;
            unsigned hex longint;
            unsigned hex longint;
            rsrcID;
            evenPaddedString;
    };
};

#define opcodeFlags                                              \
    boolean dontDeleteWhenRemoving, deleteWhenRemoving;          \
    boolean dontDeleteWhenInstalling, deleteWhenInstalling;      \
    boolean dontCopy, copy;                                      \
    fill bit[3]

#define fileAtom1Flags                                           \
    opcodeFlags;                                                 \
    boolean dontIgnoreLockedFile, ignoreLockedFile;              \
    boolean dontSetFileLocked, setFileLocked;                    \
    boolean useSrcCrDateToCompare, useVersProcToCompare;         \
    boolean srcNeedExist, srcNeedNotExist;                       \
    boolean rsrcForkInRsrcFork, rsrcForkInDataFork;              \
    boolean updateEvenIfNewer, leaveAloneIfNewer;                \
    boolean updateExisting, keepExisting;                        \
    boolean copyIfNewOrUpdate, copyIfUpdate;                     \
    boolean noRsrcFork, rsrcFork;                                \
    boolean noDataFork, dataFork

type 'infa' {
    switch {
        case format1:
            key integer = 1;
            fileAtom1Flags;
            unsigned longint;
            unsigned integer;
            rsrcID;
            integer = $$CountOf(Pieces);
            wide array Pieces {
                rsrcID;
                unsigned longint;
                unsigned longint;
            };
            unsigned hex longint;
            rsrcID;
            rsrcID;
            evenPaddedString;
    };
};

type 'infm' {
    switch {
        case format0:
            key integer = 0;
            fill bit[16];
            unsigned longint;
            rsrcID;
            rsrcID;
            evenPaddedString;
    };
};
