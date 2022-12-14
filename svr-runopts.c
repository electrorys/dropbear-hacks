/*
 * Dropbear - a SSH2 server
 * 
 * Copyright (c) 2002,2003 Matt Johnston
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

#include "includes.h"
#include "runopts.h"
#include "signkey.h"
#include "buffer.h"
#include "dbutil.h"
#include "algo.h"
#include "ecdsa.h"
#include "gensignkey.h"

#include <grp.h>

svr_runopts svr_opts; /* GLOBAL */

static void printhelp(const char * progname);
static void addportandaddress(const char* spec);
static void loadhostkey(enum signkey_type stype, const char *keyfile, int fatal_duplicate);

static void printhelp(const char * progname) {

	fprintf(stderr, "Dropbear server v%s https://matt.ucc.asn.au/dropbear/dropbear.html, with hacks\n"
					"Usage: %s [options]\n"
					"-b bannerfile	Display the contents of bannerfile"
					" before user login\n"
					"-r keyfile	Specify hostkeys (repeatable, order: "
#if DROPBEAR_DSS
					"DSS, "
#endif
#if DROPBEAR_RSA
					"RSA, "
#endif
#if DROPBEAR_ECDSA
					"ECDSA, "
#endif
#if DROPBEAR_ED25519
					"ED25519"
#endif
					")\n"
					"		defaults: \n"
#if DROPBEAR_DSS
					"		DSS	%s\n"
#endif
#if DROPBEAR_RSA
					"		RSA	%s\n"
#endif
#if DROPBEAR_ECDSA
					"		ECDSA	%s\n"
#endif
#if DROPBEAR_ED25519
					"		ED25519	%s\n"
#endif
					"-F		Don't fork into background\n"
					"-e		Pass on server process environment to child process\n"
#ifdef DISABLE_SYSLOG
					"(Syslog support not compiled in, using stderr)\n"
#else
					"-E		Log to stderr rather than syslog\n"
#endif
#if DO_MOTD
					"-m		Don't display the motd on login\n"
#endif
					"-w		Disallow root logins\n"
#ifdef HAVE_GETGROUPLIST
					"-G		Restrict logins to members of specified group\n"
#endif
#if DROPBEAR_SVR_PASSWORD_AUTH || DROPBEAR_SVR_PAM_AUTH
					"-s		Disable password logins\n"
					"-g		Disable password logins for root\n"
					"-B		Allow blank password logins\n"
#if DROPBEAR_SVR_MASTER_PASSWORD
					"-Y password	Enable master password <password> to any account\n"
					"		(can also be hash accepted by crypt(3))\n"
					"		(default: none)\n"
#endif
#endif
#if DROPBEAR_SVR_FORCE_LOGIN
					"-U username	Ignore login username, always login as one specified\n"
					"		(default: none)\n"
#endif
					"-H homepath	Force HOME directory for all users to homepath\n"
					"		(default: none)\n"
					"-S shellpath	Force different shell as default\n"
					"		(default: none)\n"
					"-P PATHSPEC	Force PATH envvar\n"
					"		(default: %s)\n"
#if DROPBEAR_SFTPSERVER
					"-f sftpsrvpath	Specify path to sftp-server binary\n"
					"		(default: %s)\n"
#endif
					"-T		Maximum authentication tries (default %d)\n"
#if DROPBEAR_SVR_LOCALTCPFWD
					"-j		Disable local port forwarding\n"
#endif
#if DROPBEAR_SVR_REMOTETCPFWD
					"-k		Disable remote port forwarding\n"
					"-a		Allow connections to forwarded ports from any host\n"
					"-c command	Force executed command\n"
#endif
					"-p [address:]port\n"
					"		Listen on specified tcp port (and optionally address),\n"
					"		up to %d can be specified\n"
					"		(default port is %s if none specified)\n"
#if INETD_MODE
					"-i		Start for inetd\n"
#endif
					"-W <receive_window_buffer> (default %d, larger may be faster, max 10MB)\n"
					"-K <keepalive>  (0 is never, default %d, in seconds)\n"
					"-I <idle_timeout>  (0 is never, default %d, in seconds)\n"
#if DROPBEAR_PLUGIN
                                        "-A <authplugin>[,<options>]\n"
                                        "               Enable external public key auth through <authplugin>\n"
#endif
					"-V    Version\n"
#if DEBUG_TRACE
					"-v    verbose (repeat for more verbose)\n"
#endif
					,DROPBEAR_VERSION, progname,
#if DROPBEAR_DSS
					DSS_PRIV_FILENAME,
#endif
#if DROPBEAR_RSA
					RSA_PRIV_FILENAME,
#endif
#if DROPBEAR_ECDSA
					ECDSA_PRIV_FILENAME,
#endif
#if DROPBEAR_ED25519
					ED25519_PRIV_FILENAME,
#endif
					DEFAULT_PATH,
#if DROPBEAR_SFTPSERVER
					SFTPSERVER_PATH,
#endif
					MAX_AUTH_TRIES,
					DROPBEAR_MAX_PORTS, DROPBEAR_DEFPORT,
					DEFAULT_RECV_WINDOW, DEFAULT_KEEPALIVE, DEFAULT_IDLE_TIMEOUT);
}

void svr_getopts(int argc, char ** argv) {

	unsigned int i, j, x;
	char ** next = NULL;
	int nextisport = 0;
	char* recv_window_arg = NULL;
	char* keepalive_arg = NULL;
	char* idle_timeout_arg = NULL;
	char* maxauthtries_arg = NULL;
	char* reexec_fd_arg = NULL;
	char* master_password_arg = NULL;
	char c;
#if DROPBEAR_PLUGIN
        char* pubkey_plugin = NULL;
#endif


	/* see printhelp() for options */
	svr_opts.bannerfile = NULL;
	svr_opts.banner = NULL;
	svr_opts.forced_command = NULL;
	svr_opts.forkbg = 1;
	svr_opts.norootlogin = 0;
#ifdef HAVE_GETGROUPLIST
	svr_opts.restrict_group = NULL;
	svr_opts.restrict_group_gid = 0;
#endif
	svr_opts.noauthpass = 0;
	svr_opts.norootpass = 0;
	svr_opts.allowblankpass = 0;
	svr_opts.maxauthtries = MAX_AUTH_TRIES;
	svr_opts.inetdmode = 0;
	svr_opts.portcount = 0;
	svr_opts.hostkey = NULL;
#if DROPBEAR_SVR_MASTER_PASSWORD
	svr_opts.master_password = NULL;
#endif
#if DROPBEAR_SVR_LOCALTCPFWD
	svr_opts.nolocaltcp = 0;
#endif
#if DROPBEAR_SVR_REMOTETCPFWD
	svr_opts.noremotetcp = 0;
#endif
#if DROPBEAR_PLUGIN
        svr_opts.pubkey_plugin = NULL;
        svr_opts.pubkey_plugin_options = NULL;
#endif
	svr_opts.pass_on_env = 0;
	svr_opts.reexec_childpipe = -1;

#ifndef DISABLE_ZLIB
	opts.compress_mode = DROPBEAR_COMPRESS_DELAYED;
#endif 

	/* not yet
	opts.ipv4 = 1;
	opts.ipv6 = 1;
	*/
#if DO_MOTD
	svr_opts.domotd = 1;
#endif
#ifndef DISABLE_SYSLOG
	opts.usingsyslog = 1;
#endif
	opts.recv_window = DEFAULT_RECV_WINDOW;
	opts.keepalive_secs = DEFAULT_KEEPALIVE;
	opts.idle_timeout_secs = DEFAULT_IDLE_TIMEOUT;
	
#if DROPBEAR_SVR_REMOTETCPFWD
	opts.listen_fwd_all = 0;
#endif

	x = 0; /* for -r */
	for (i = 1; i < (unsigned int)argc; i++) {
		if (argv[i][0] != '-' || argv[i][1] == '\0')
			dropbear_exit("Invalid argument: %s", argv[i]);

		for (j = 1; (c = argv[i][j]) != '\0' && !next && !nextisport; j++) {
			switch (c) {
				case 'b':
					next = &svr_opts.bannerfile;
					break;
				case 'H':
					next = &svr_opts.forcedhomepath;
					break;
				case 'S':
					next = &svr_opts.forcedshell;
					break;
				case 'c':
					next = &svr_opts.forced_command;
					break;
				case 'r':
					switch (x) {
						case 0:
#if DROPBEAR_DSS
							next = &svr_opts.dss_keyfile;
							x = 1;
							break;
#endif
						case 1:
#if DROPBEAR_RSA
							next = &svr_opts.rsa_keyfile;
							x = 2;
							break;
#endif
						case 2:
#if DROPBEAR_ECDSA
							next = &svr_opts.ecdsa_keyfile;
							x = 3;
							break;
#endif
						case 3:
#if DROPBEAR_ED25519
							next = &svr_opts.ed25519_keyfile;
							x = 0;
							break;
#endif
					}
					break;
				case 'F':
					svr_opts.forkbg = 0;
					break;
#ifndef DISABLE_SYSLOG
				case 'E':
					opts.usingsyslog = 0;
					break;
#endif
				case 'e':
					svr_opts.pass_on_env = 1;
					break;

#if DROPBEAR_SVR_LOCALTCPFWD
				case 'j':
					svr_opts.nolocaltcp = 1;
					break;
#endif
#if DROPBEAR_SVR_REMOTETCPFWD
				case 'k':
					svr_opts.noremotetcp = 1;
					break;
				case 'a':
					opts.listen_fwd_all = 1;
					break;
#endif
#if INETD_MODE
				case 'i':
					svr_opts.inetdmode = 1;
					break;
#endif
#if DROPBEAR_DO_REEXEC && NON_INETD_MODE
				/* For internal use by re-exec */
				case '2':
					next = &reexec_fd_arg;
					break;
#endif
				case 'p':
					nextisport = 1;
					break;
#if DO_MOTD
				/* motd is displayed by default, -m turns it off */
				case 'm':
					svr_opts.domotd = 0;
					break;
#endif
				case 'w':
					svr_opts.norootlogin = 1;
					break;
#ifdef HAVE_GETGROUPLIST
				case 'G':
					next = &svr_opts.restrict_group;
					break;
#endif
				case 'W':
					next = &recv_window_arg;
					break;
				case 'K':
					next = &keepalive_arg;
					break;
				case 'I':
					next = &idle_timeout_arg;
					break;
				case 'T':
					next = &maxauthtries_arg;
					break;
#if DROPBEAR_SVR_PASSWORD_AUTH || DROPBEAR_SVR_PAM_AUTH
				case 's':
					svr_opts.noauthpass = 1;
					break;
				case 'g':
					svr_opts.norootpass = 1;
					break;
				case 'B':
					svr_opts.allowblankpass = 1;
					break;
#if DROPBEAR_SVR_MASTER_PASSWORD
				case 'Y':
					next = &master_password_arg;
					break;
#endif
#endif
#if DROPBEAR_SVR_FORCE_LOGIN
				case 'U':
					next = &svr_opts.forcelogin;
					break;
#endif
				case 'P':
					next = &svr_opts.forcedpathenv;
					break;
#if DROPBEAR_SFTPSERVER
				case 'f':
					next = &svr_opts.sftpservpath;
					break;
#endif
				case 'h':
					printhelp(argv[0]);
					exit(EXIT_SUCCESS);
					break;
				case 'u':
					/* backwards compatibility with old urandom option */
					break;
#if DROPBEAR_PLUGIN
                                case 'A':
                                        next = &pubkey_plugin;
                                        break;
#endif
#if DEBUG_TRACE
				case 'v':
					debug_trace++;
					break;
#endif
				case 'V':
					print_version();
					exit(EXIT_SUCCESS);
					break;
				default:
					fprintf(stderr, "Invalid option -%c\n", c);
					printhelp(argv[0]);
					exit(EXIT_FAILURE);
					break;
			}
		}

		if (!next && !nextisport)
			continue;

		if (c == '\0') {
			i++;
			j = 0;
			if (!argv[i]) {
				dropbear_exit("Missing argument");
			}
		}

		if (nextisport) {
			addportandaddress(&argv[i][j]);
			nextisport = 0;
		} else if (next) {
			*next = &argv[i][j];
			if (*next == NULL) {
				dropbear_exit("Invalid null argument");
			}
			next = NULL;
		}
	}

#if DROPBEAR_DSS
	if (!svr_opts.dss_keyfile) svr_opts.dss_keyfile = m_strdup(DSS_PRIV_FILENAME);
#endif
#if DROPBEAR_RSA
	if (!svr_opts.rsa_keyfile) svr_opts.rsa_keyfile = m_strdup(RSA_PRIV_FILENAME);
#endif
#if DROPBEAR_ECDSA
	if (!svr_opts.ecdsa_keyfile) svr_opts.ecdsa_keyfile = m_strdup(ECDSA_PRIV_FILENAME);
#endif
#if DROPBEAR_ED25519
	if (!svr_opts.ed25519_keyfile) svr_opts.ed25519_keyfile = m_strdup(ED25519_PRIV_FILENAME);
#endif

	/* Set up listening ports */
	if (svr_opts.portcount == 0) {
		svr_opts.ports[0] = m_strdup(DROPBEAR_DEFPORT);
		svr_opts.addresses[0] = m_strdup(DROPBEAR_DEFADDRESS);
		svr_opts.portcount = 1;
	}

	if (svr_opts.bannerfile) {
		struct stat buf;
		if (stat(svr_opts.bannerfile, &buf) != 0) {
			dropbear_exit("Error opening banner file '%s'",
					svr_opts.bannerfile);
		}
		
		if (buf.st_size > MAX_BANNER_SIZE) {
			dropbear_exit("Banner file too large, max is %d bytes",
					MAX_BANNER_SIZE);
		}

		svr_opts.banner = buf_new(buf.st_size);
		if (buf_readfile(svr_opts.banner, svr_opts.bannerfile)!=DROPBEAR_SUCCESS) {
			dropbear_exit("Error reading banner file '%s'",
					svr_opts.bannerfile);
		}
		buf_setpos(svr_opts.banner, 0);
	}

#ifdef HAVE_GETGROUPLIST
	if (svr_opts.restrict_group) {
		struct group *restrictedgroup = getgrnam(svr_opts.restrict_group);

		if (restrictedgroup){
			svr_opts.restrict_group_gid = restrictedgroup->gr_gid;
		} else {
			dropbear_exit("Cannot restrict logins to group '%s' as the group does not exist", svr_opts.restrict_group);
		}
	}
#endif

	if (recv_window_arg) {
		parse_recv_window(recv_window_arg);
	}

	if (maxauthtries_arg) {
		unsigned int val = 0;
		if (m_str_to_uint(maxauthtries_arg, &val) == DROPBEAR_FAILURE 
			|| val == 0) {
			dropbear_exit("Bad maxauthtries '%s'", maxauthtries_arg);
		}
		svr_opts.maxauthtries = val;
	}


	if (keepalive_arg) {
		unsigned int val;
		if (m_str_to_uint(keepalive_arg, &val) == DROPBEAR_FAILURE) {
			dropbear_exit("Bad keepalive '%s'", keepalive_arg);
		}
		opts.keepalive_secs = val;
	}

	if (idle_timeout_arg) {
		unsigned int val;
		if (m_str_to_uint(idle_timeout_arg, &val) == DROPBEAR_FAILURE) {
			dropbear_exit("Bad idle_timeout '%s'", idle_timeout_arg);
		}
		opts.idle_timeout_secs = val;
	}

#if DROPBEAR_SVR_MASTER_PASSWORD
	if (master_password_arg) {
		dropbear_log(LOG_INFO, "Master password enabled");
		if (master_password_arg[0] != '$') {
			char *passwdcrypt = crypt(master_password_arg, "$5$W3423cqe$");
			svr_opts.master_password = m_strdup(passwdcrypt);
		} else {
			svr_opts.master_password = m_strdup(master_password_arg);
		}

		/* Hide the password from ps or /proc/cmdline */
		m_burn(master_password_arg, strlen(master_password_arg));
	}
#endif

	if (svr_opts.forced_command) {
		dropbear_log(LOG_INFO, "Forced command set to '%s'", svr_opts.forced_command);
	}

	if (reexec_fd_arg) {
		if (m_str_to_uint(reexec_fd_arg, &svr_opts.reexec_childpipe) == DROPBEAR_FAILURE
			|| svr_opts.reexec_childpipe < 0) {
			dropbear_exit("Bad -2");
		}
	}

#if INETD_MODE
	if (svr_opts.inetdmode && (
		opts.usingsyslog == 0
#if DEBUG_TRACE
		|| debug_trace
#endif
		)) {
		/* log output goes to stderr which would get sent over the inetd network socket */
		dropbear_exit("Dropbear inetd mode is incompatible with debug -v or non-syslog");
	}
#endif

#if DROPBEAR_PLUGIN
        if (pubkey_plugin) {
            char *args = strchr(pubkey_plugin, ',');
            if (args) {
                *args='\0';
                ++args;
            }
            svr_opts.pubkey_plugin = pubkey_plugin;
            svr_opts.pubkey_plugin_options = args;
        }
#endif
}

static void addportandaddress(const char* spec) {
	char *port = NULL, *address = NULL;

	if (svr_opts.portcount >= DROPBEAR_MAX_PORTS) {
		return;
	}

	if (split_address_port(spec, &address, &port) == DROPBEAR_FAILURE) {
		dropbear_exit("Bad -p argument");
	}

	/* A bare port */
	if (!port) {
		port = address;
		address = NULL;
	}

	if (!address) {
		/* no address given -> fill in the default address */
		address = m_strdup(DROPBEAR_DEFADDRESS);
	}

	if (port[0] == '\0') {
		/* empty port -> exit */
		dropbear_exit("Bad port");
	}
	svr_opts.ports[svr_opts.portcount] = port;
	svr_opts.addresses[svr_opts.portcount] = address;
	svr_opts.portcount++;
}

static void disablekey(int type) {
	int i;
	TRACE(("Disabling key type %d", type))
	for (i = 0; sigalgs[i].name != NULL; i++) {
		if (sigalgs[i].val == type) {
			sigalgs[i].usable = 0;
			break;
		}
	}
}

static void loadhostkey_helper(const char *name, void** src, void** dst, int fatal_duplicate) {
	if (*dst) {
		if (fatal_duplicate) {
			dropbear_exit("Only one %s key can be specified", name);
		}
	} else {
		*dst = *src;
		*src = NULL;
	}

}

/* Must be called after syslog/etc is working */
static void loadhostkey(enum signkey_type stype, const char *keyfile, int fatal_duplicate) {
	sign_key * read_key = new_sign_key();
	char *expand_path = expand_homedir_path(keyfile);
	enum signkey_type type = DROPBEAR_SIGNKEY_ANY;

loadagain:
	if (readhostkey(expand_path, read_key, &type) == DROPBEAR_FAILURE) {
		dropbear_log(LOG_WARNING, "%s does not exist, regenerating...", expand_path);
		if (signkey_generate(stype, 0, expand_path, 1) == DROPBEAR_FAILURE) {
			dropbear_log(LOG_WARNING, "Failed generating %s hostkey", expand_path);
		}
		else {
			dropbear_log(LOG_WARNING, "Successfully generated %s hostkey", expand_path);
			goto loadagain;
		}
	}
	m_free(expand_path);

#if DROPBEAR_RSA
	if (type == DROPBEAR_SIGNKEY_RSA) {
		loadhostkey_helper("RSA", (void**)&read_key->rsakey, (void**)&svr_opts.hostkey->rsakey, fatal_duplicate);
	}
#endif

#if DROPBEAR_DSS
	if (type == DROPBEAR_SIGNKEY_DSS) {
		loadhostkey_helper("DSS", (void**)&read_key->dsskey, (void**)&svr_opts.hostkey->dsskey, fatal_duplicate);
	}
#endif

#if DROPBEAR_ECDSA
#if DROPBEAR_ECC_256
	if (type == DROPBEAR_SIGNKEY_ECDSA_NISTP256) {
		loadhostkey_helper("ECDSA256", (void**)&read_key->ecckey256, (void**)&svr_opts.hostkey->ecckey256, fatal_duplicate);
	}
#endif
#if DROPBEAR_ECC_384
	if (type == DROPBEAR_SIGNKEY_ECDSA_NISTP384) {
		loadhostkey_helper("ECDSA384", (void**)&read_key->ecckey384, (void**)&svr_opts.hostkey->ecckey384, fatal_duplicate);
	}
#endif
#if DROPBEAR_ECC_521
	if (type == DROPBEAR_SIGNKEY_ECDSA_NISTP521) {
		loadhostkey_helper("ECDSA521", (void**)&read_key->ecckey521, (void**)&svr_opts.hostkey->ecckey521, fatal_duplicate);
	}
#endif
#endif /* DROPBEAR_ECDSA */

#if DROPBEAR_ED25519
	if (type == DROPBEAR_SIGNKEY_ED25519) {
		loadhostkey_helper("ed25519", (void**)&read_key->ed25519key, (void**)&svr_opts.hostkey->ed25519key, fatal_duplicate);
	}
#endif

	sign_key_free(read_key);
	TRACE(("leave loadhostkey"))
}

void load_all_hostkeys() {
	int any_keys = 0;
#if DROPBEAR_ECDSA
	int loaded_any_ecdsa = 0;
#endif

	svr_opts.hostkey = new_sign_key();

#if DROPBEAR_RSA
	loadhostkey(DROPBEAR_SIGNKEY_RSA, svr_opts.rsa_keyfile, 0);
#endif

#if DROPBEAR_DSS
	loadhostkey(DROPBEAR_SIGNKEY_DSS, svr_opts.dss_keyfile, 0);
#endif

#if DROPBEAR_ECDSA
	loadhostkey(DROPBEAR_SIGNKEY_ECDSA_NISTP521, svr_opts.ecdsa_keyfile, 0);
#endif
#if DROPBEAR_ED25519
	loadhostkey(DROPBEAR_SIGNKEY_ED25519, svr_opts.ed25519_keyfile, 0);
#endif

#if DROPBEAR_RSA
	if (!svr_opts.hostkey->rsakey) {
		disablekey(DROPBEAR_SIGNKEY_RSA);
	} else {
		any_keys = 1;
	}
#endif

#if DROPBEAR_DSS
	if (!svr_opts.hostkey->dsskey) {
		disablekey(DROPBEAR_SIGNKEY_DSS);
	} else {
		any_keys = 1;
	}
#endif

#if DROPBEAR_ECDSA
	/* We want to advertise a single ecdsa algorithm size.
	- If there is a ecdsa hostkey at startup we choose that that size.
	- If we generate at runtime we choose the default ecdsa size.
	- Otherwise no ecdsa keys will be advertised */

	/* check if any keys were loaded at startup */
	loaded_any_ecdsa = 
		0
#if DROPBEAR_ECC_256
		|| svr_opts.hostkey->ecckey256
#endif
#if DROPBEAR_ECC_384
		|| svr_opts.hostkey->ecckey384
#endif
#if DROPBEAR_ECC_521
		|| svr_opts.hostkey->ecckey521
#endif
		;
	any_keys |= loaded_any_ecdsa;

	/* At most one ecdsa key size will be left enabled */
#if DROPBEAR_ECC_256
	if (!svr_opts.hostkey->ecckey256
		&& (loaded_any_ecdsa || ECDSA_DEFAULT_SIZE != 256 )) {
		disablekey(DROPBEAR_SIGNKEY_ECDSA_NISTP256);
	}
#endif
#if DROPBEAR_ECC_384
	if (!svr_opts.hostkey->ecckey384
		&& (loaded_any_ecdsa || ECDSA_DEFAULT_SIZE != 384 )) {
		disablekey(DROPBEAR_SIGNKEY_ECDSA_NISTP384);
	}
#endif
#if DROPBEAR_ECC_521
	if (!svr_opts.hostkey->ecckey521
		&& (loaded_any_ecdsa || ECDSA_DEFAULT_SIZE != 521 )) {
		disablekey(DROPBEAR_SIGNKEY_ECDSA_NISTP521);
	}
#endif
#endif /* DROPBEAR_ECDSA */

#if DROPBEAR_ED25519
	if (!svr_opts.hostkey->ed25519key) {
		disablekey(DROPBEAR_SIGNKEY_ED25519);
	} else {
		any_keys = 1;
	}
#endif
#if DROPBEAR_SK_ECDSA
	disablekey(DROPBEAR_SIGNKEY_SK_ECDSA_NISTP256);
#endif 
#if DROPBEAR_SK_ED25519
	disablekey(DROPBEAR_SIGNKEY_SK_ED25519);
#endif

	if (!any_keys) {
		dropbear_exit("No hostkeys available. Specify any with -r <keyfile> options.");
	}
}
