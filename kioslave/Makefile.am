if include_kioslave_ldap
LDAP_SUBDIR=ldap
endif

if include_kioslave_smb
SMB_SUBDIR=smb
endif

SUBDIRS = about filter fish info man pop3 smtp \
  sftp tar thumbnail $(LDAP_SUBDIR) $(SMB_SUBDIR) settings trash media \
  remote home system
