import re

PACKAGE_REGEX = ".*\.rpm(\.asc)?$"

INTERESTING_RE = [
	{
		"name" : "PEM_files",
		"desc" : "PEM files",
		"regex" : ".*\.pem$",
		"have" : []
	},
	{
		"name" : "Cert_files",
		"desc" : "Cert files",
		"regex" : "(^.*\.((crt)|(cert)|(cer)))$",
		"have" : []
	},
	{
		"name" : "DER_files",
		"desc" : "DER files",
		"regex" : ".*\.der$",
		"have" : []
	},
	{
		"name" : "Putty_keys",
		"desc" : "Putty files",
		"regex" : ".*\.ppk$",
		"have" : []
	},
	{
		"name" : "Key_files",
		"desc" : "Key files",
		"regex" : ".*\.key$",
		"have" : []
	},
	{
		"name" : "PKCS_12_files",
		"desc" : "PKCS 12 filetypes",
		"regex" : ".*\.((p12)|(pfx))$",
		"have" : []
	},
	{
		"name" : "PKCS_7_files",
		"desc" : "PKCS 7 filetypes",
		"regex" : ".*\.p7b$",
		"have" : []
	},
	{
		"name" : "Cert_Req_files",
		"desc" : "Certificate signature request files",
		"regex" : ".*\.((req)|(csr))$",
                "have" : []
	},
	{
		"name" : "Pub_files",
		"desc" : ".pub files (look for w/o .pub)",
		"regex" : ".*\.pub$",
		"have" : []
	},
	{
		"name" : "SAM_files",
		"desc" : "SAM files",
		"regex" : "^SAM$",
		"have" : []
	},
	{
		"name" : "shadow_files",
		"desc" : "shadow files",
		"regex" : "^shadow$",
		"have" : []
	},
	{
		"name" : "ssh_host_keys",
		"desc" : "Private SSH key files",
		"regex" : "^((ssh_host_.*_key)|(dropbear_((rsa)|(dsa))_host_key))$",
		"have" : []
	},
	{
		"name" : "keepassx_files",
		"desc" : "Password Mgr db's",
		"regex" : "^.*\.((kdbx)|(kdb)|(agilekeychain))$",
		"have" : []
	},
]

# Removed filters
"""
	{
		"name" : "htaccess_files",
		"desc" : ".htaccess files",
		"regex" : "^.htaccess$",
		"have" : []
	},
	{
		"name" : "manual_files",
		"desc" : "Manuals",
		"regex" : "^.*((manual)|(operator)).*$",
		"have" : []
	},
	{
		"name" : "stingray_files",
		"desc" : "StingRay",
		"regex" : "^.*stingray.*$",
		"have" : []
	},
	{
		"name" : "drt_files",
		"desc" : "DRT box",
		"regex" : "^.*((whitebox)|(drt)|(dirt[^y])).*$",
                "exclude" : ".*((cdrtools)|(dvdrtools)).*",
		"have" : []
	},
	{
		"name" : "Cellphone_selectors",
		"desc" : "Selectors for cellphone information",
		"regex" : ".*((imsi)|(msin)|(msisdn)|(timsi)|(gsm)|(cdma)).*",
		"have" : []
	},
	{
		"name" : "Cisco_VPN_dbs",
		"desc" : "Login and passwords for Cisco VPNs",
		"regex" : ".*\.pcf$",
		"have" : []
	},
	{
		"name" : "WordPress_config_files",
		"desc" : "WordPress config file",
		"regex" : "^wp-config.php$",
		"have" : []
	},
	{
		"name" : "syslog_files",
		"desc" : "syslog file",
		"regex" : "^syslog$",
		"have" : []
	},
	{
		"name" : "database_files",
		"desc" : "Database files",
		"regex" : "^.*\.((sql)|(db))$",
		"exclude" : "^Thumbs.db$",
		"have" : []
	},
	{
		"name" : "known_hosts_files",
		"desc" : "known_hosts files",
		"regex" : "^known_hosts$",
		"have" : []
	},
	{
		"name" : "authorized_keys_files",
		"desc" : "authorized_keys files",
		"regex" : "^authorized_keys$",
		"have" : []
	},
	{
		"name" : "has_SSH",
		"desc" : "SSH found",
		"regex" : ".*ssh.*",
		"have" : [],
		"exclude" : PACKAGE_REGEX
	},
	{
		"name" : "passw_files",
		"desc" : "'passw' files",
		"regex" : ".*passw.*",
		"have" : [],
		"exclude" : PACKAGE_REGEX
	},
"""
