/*
 * Copyright (C) 2011-2012 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author: tasleson
 */

#ifndef LSM_NFS_EXPORT_H
#define LSM_NFS_EXPORT_H

#include "libstoragemgmt_types.h"


#ifdef  __cplusplus
extern "C" {
#endif

#define ANON_UID_GID_NA     -1
#define ANON_UID_GID_ERROR (ANON_UID_GID_NA - 1)

/**
 * Allocated memory for a NFS export record
 * @param id            Export ID  (Set to NULL when creating new export)
 * @param fs_id         File system ID that is exported
 * @param export_path   The desired path for the export
 * @param auth          NFS client authentication type  (May be NULL)
 * @param root          List of hosts that have root access (May be NULL)
 * @param rw            List of hosts that have read/write access (May be NULL)
 * @param ro            List of hosts that have read only access (May be NULL)
 * @param anonuid       User id that should be mapped to anonymous
 *                      (Valid or set to ANON_UID_GID_NA).
 * @param anongid       Group id that should be mapped to anonymous
 *                      (Valid or set to ANON_UID_GID_NA)
 * @param options       String of options passed to array
 * @return Valid lsmNfsExportPtr, else NULL on error.
 */
lsmNfsExportPtr LSM_DLL_EXPORT lsmNfsExportRecordAlloc( const char *id,
                                                    const char *fs_id,
                                                    const char *export_path,
                                                    const char *auth,
                                                    lsmStringListPtr root,
                                                    lsmStringListPtr rw,
                                                    lsmStringListPtr ro,
                                                    uint64_t anonuid,
                                                    uint64_t anongid,
                                                    const char *options);

/**
 * Allocated the memory for an array of NFS export records.
 * @param size          Number of elements
 * @return Allocated memory, NULL on error
 */
lsmNfsExportPtr LSM_DLL_EXPORT *lsmNfsExportRecordAllocArray( uint32_t size );


/**
 * Frees the memory for a NFS export record.
 * @param exp
 */
void LSM_DLL_EXPORT lsmNfsExportRecordFree( lsmNfsExportPtr exp );

/**
 * Frees the memory for the NFS export array and the memory for each entry
 * @param exps          Memory to free
 * @param size          Number of entries
 */
void LSM_DLL_EXPORT lsmNfsExportRecordFreeArray( lsmNfsExportPtr exps[],
                                                    uint32_t size);

/**
 * Duplicates the source and returns the copy.
 * @param source            Source record to copy
 * @return Copy of source, else NULL one error.
 */
lsmNfsExportPtr LSM_DLL_EXPORT lsmNfsExportRecordCopy( lsmNfsExportPtr source );

/**
 * Returns the ID
 * @param exp       Valid nfs export record
 * @return Pointer to ID
 */
const char LSM_DLL_EXPORT *lsmNfsExportIdGet( lsmNfsExportPtr exp );
int LSM_DLL_EXPORT lsmNfsExportIdSet(lsmNfsExportPtr exp, const char *ep );

/**
 * Returns the file system id
 * @param exp       Valid nfs export record
 * @return Pointer to file system id
 */
const char LSM_DLL_EXPORT *lsmNfsExportFsIdGet( lsmNfsExportPtr exp );
int LSM_DLL_EXPORT lsmNfsExportFsIdSet(lsmNfsExportPtr exp, const char *fs_id);

/**
 * Returns the export path
 * @param exp       Valid nfs export record
 * @return Pointer to export path
 */
const char LSM_DLL_EXPORT *lsmNfsExportExportPathGet( lsmNfsExportPtr exp );
int LSM_DLL_EXPORT lsmNfsExportExportPathSet( lsmNfsExportPtr exp,
                                                const char *export_path);

/**
 * Returns the client authentication type
 * @param exp       Valid nfs export record
 * @return Pointer to authentication type
 */
const char LSM_DLL_EXPORT *lsmNfsExportAuthTypeGet( lsmNfsExportPtr exp );
int LSM_DLL_EXPORT lsmNfsExportAuthTypeSet( lsmNfsExportPtr exp,
                                            const char *value );

/**
 * Returns the list of hosts that have root access
 * @param exp       Valid nfs export record
 * @return list of hosts.
 */
lsmStringListPtr LSM_DLL_EXPORT lsmNfsExportRootGet( lsmNfsExportPtr exp);
int LSM_DLL_EXPORT lsmNfsExportRootSet(lsmNfsExportPtr exp,
                                        lsmStringListPtr value);

/**
 * Returns the list of hosts that have read/write access to export.
 * @param exp       Valid nfs export record
 * @return list of hosts.
 */
lsmStringListPtr LSM_DLL_EXPORT lsmNfsExportReadWriteGet( lsmNfsExportPtr exp);
int LSM_DLL_EXPORT lsmNfsExportReadWriteSet( lsmNfsExportPtr exp,
                                                lsmStringListPtr value);

/**
 * Returns the list of hosts that have read only access to export.
 * @param exp       Valid nfs export record
 * @return list of hosts
 */
lsmStringListPtr LSM_DLL_EXPORT lsmNfsExportReadOnlyGet( lsmNfsExportPtr exp );
int LSM_DLL_EXPORT lsmNfsExportReadOnlySet( lsmNfsExportPtr exp,
                                            lsmStringListPtr value);

/**
 * Returns the id which is to be mapped to anonymous id
 * @param exp       Valid nfs export record
 * @return ANON_UID_GID_NA value is returned when this isn't set, else value
 * mapped to anonymous group id.  For errors ANON_UID_GID_ERROR is returned.
 */
uint64_t LSM_DLL_EXPORT lsmNfsExportAnonUidGet( lsmNfsExportPtr exp );
int LSM_DLL_EXPORT lsmNfsExportAnonUidSet( lsmNfsExportPtr exp, uint64_t value);

/**
 * Returns the group id which is to be mapped to anonymous group
 * @param exp       Valid nfs export record
 * @return ANON_UID_GID_NA value is returned when this isn't set, else value
 * mapped to anonymous group id.  For errors ANON_UID_GID_ERROR is returned.
 */
uint64_t LSM_DLL_EXPORT lsmNfsExportAnonGidGet( lsmNfsExportPtr exp );
int LSM_DLL_EXPORT lsmNfsExportAnonGidSet( lsmNfsExportPtr exp, uint64_t value);

/**
 * Returns the options for this export.
 * @param exp       Valid nfs export record
 * @return Options value, NULL if not applicable.
 */
const char LSM_DLL_EXPORT *lsmNfsExportOptionsGet( lsmNfsExportPtr exp);
int LSM_DLL_EXPORT lsmNfsExportOptionsSet( lsmNfsExportPtr exp,
                                            const char *value);


#ifdef  __cplusplus
}
#endif

#endif