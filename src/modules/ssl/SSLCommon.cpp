/*
	Copyright 2009-2012, Sumeet Chhetri

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
 */
/*
 * SSLCommon.cpp
 *
 *  Created on: 19-Dec-2013
 *      Author: sumeetc
 */

#include "SSLCommon.h"

string SSLCommon::ciphers =
    string("ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-") +
	string("AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:") +
	string("DHE-DSS-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-") +
	string("AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-") +
	string("AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-") +
	string("AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA256:") +
	string("DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:!aNULL:!eNULL:") +
	string("!EXPORT:!DES:!RC4:!3DES:!MD5:!PSK");

SSLCommon::SSLCommon() {
	// TODO Auto-generated constructor stub

}

SSLCommon::~SSLCommon() {
	// TODO Auto-generated destructor stub
}


void SSLCommon::exitSSL(const char *func)
{
	fprintf (stdout, "%s failed:\n", func);
	/* This is the OpenSSL function that prints the contents of the
	 * error stack to the specified file handle. */
	ERR_print_errors_fp (stdout);
	exit (EXIT_FAILURE);
}

void SSLCommon::load_dh_params(SSL_CTX *ctx, char *file)
{
	DH *ret = 0;
	BIO *bio;

	if ((bio=BIO_new_file(file,"r")) == NULL)
	{
		cout << "Couldn't open DH file, defaulting to setting DH to single use" << endl;
		SSL_CTX_set_options(ctx, SSL_OP_SINGLE_DH_USE);
		return;
	}

	ret = PEM_read_bio_DHparams(bio,NULL,NULL,NULL);
	if(SSL_CTX_set_tmp_dh(ctx,ret)<0)
	{
		cout << "Couldn't set DH parameters" << endl;
		exitSSL("SSL_CTX_set_tmp_dh(dhfile)");
	}
	DH_free(ret);
	BIO_free(bio);
}

void SSLCommon::load_ecdh_params(SSL_CTX *ctx)
{
	/*SSL_CTX_set_options(ctx,
			SSL_OP_SINGLE_DH_USE |
			SSL_OP_SINGLE_ECDH_USE |
			SSL_OP_NO_SSLv2);
	*/
	/* Cheesily pick an elliptic curve to use with elliptic curve ciphersuites.
	 * We just hardcode a single curve which is reasonably decent.
	 * See http://www.mail-archive.com/openssl-dev@openssl.org/msg30957.html */
	EC_KEY *ecdh = EC_KEY_new_by_curve_name (NID_X9_62_prime256v1);
	if (! ecdh)
	{
		cout << "Couldn't create elliptic curve" << endl;
		exitSSL("EC_KEY_new_by_curve_name(ecdh)");
	}
	if (1 != SSL_CTX_set_tmp_ecdh (ctx, ecdh))
	{
		cout << "Couldn't set ecdh parameters" << endl;
		exitSSL("SSL_CTX_set_tmp_ecdh(ecdh)");
	}
	EC_KEY_free(ecdh);
}

/* OpenSSL has a habit of using uninitialized memory.  (They turn up their
 * nose at tools like valgrind.)  To avoid spurious valgrind errors (as well
 * as to allay any concerns that the uninitialized memory is actually
 * affecting behavior), let's install a custom malloc function which is
 * actually calloc.
 */
void* SSLCommon::zeroingMalloc (size_t howmuch)
{
	return calloc (1, howmuch);
}

SSL_CTX *SSLCommon::initialize_ctx(const bool& isServer)
{
	SSL_METHOD *meth;
	SSL_CTX *ctx;

	CRYPTO_set_mem_functions (zeroingMalloc, realloc, free);
	SSL_library_init ();
	OpenSSL_add_all_algorithms();		/* load & register all cryptos, etc. */
	SSL_load_error_strings();			/* load all error messages */

	printf ("Using OpenSSL version \"%s\"\n", SSLeay_version (SSLEAY_VERSION));

	/* Set up a SIGPIPE handler */
	signal (SIGPIPE, SIG_IGN);

	/* Create our context*/
	if(isServer)
	{
		meth = (SSL_METHOD*)SSLv23_server_method();		/* create new server-method instance */
	}
	else
	{
		meth = (SSL_METHOD*)SSLv23_client_method();		/* create new client-method instance */
	}

	ctx = SSL_CTX_new(meth);			/* create new context from method */

	if (ctx == NULL)
	{
		cout << "Could not create context" << endl;
		ERR_print_errors_fp(stderr);
		exitSSL ("SSL_CTX_new");
	}

	return ctx;
}


void SSLCommon::loadCerts(SSL_CTX* ctx, char* certFile, char* keyFile, const string& caList, const bool& ldcrts)
{
	if(ldcrts)
	{
		/* set the private key from KeyFile (may be the same as CertFile) */
		if (1 != SSL_CTX_use_PrivateKey_file(ctx, keyFile, SSL_FILETYPE_PEM))
		{
			ERR_print_errors_fp(stderr);
			cout << "Couldn't read private key file" << endl;
			exitSSL("SSL_CTX_use_PrivateKey_file");
		}
		/* set the local certificate from CertFile */
		if (1 != SSL_CTX_use_certificate_chain_file(ctx, certFile) )
		{
			ERR_print_errors_fp(stderr);
			cout << "Couldn't read certificate chain file" << endl;
			exitSSL("SSL_CTX_use_certificate_chain_file");
		}
		/* verify private key */
		if (1 != SSL_CTX_check_private_key(ctx))
		{
			cout << "Private key does not match the public certificate" << endl;
			exitSSL("SSL_CTX_check_private_key");
		}
	}

    /* Load the CAs we trust*/
    if(!(SSL_CTX_load_verify_locations(ctx, caList.c_str(),0)))
    {
    	cout << "Can't read CA list" << endl;
    	exitSSL("SSL_CTX_load_verify_locations");
    }
}

/* Check that the common name matches the
   host name*/
void SSLCommon::check_cert(SSL *ssl, char *host)
{
	X509 *peer;
	char peer_CN[256];

	if(SSL_get_verify_result(ssl)!=X509_V_OK)
		exitSSL("Certificate doesn't verify");

	/*Check the cert chain. The chain length
  	  is automatically checked by OpenSSL when
  	  we set the verify depth in the ctx */

	/*Check the common name*/
	peer=SSL_get_peer_certificate(ssl);
	X509_NAME_get_text_by_NID(X509_get_subject_name(peer), NID_commonName, peer_CN, 256);
	if(strcasecmp(peer_CN,host))
		exitSSL("Common name doesn't match host name");
}


void SSLCommon::closeSSL(const int& fd, SSL *ssl, BIO* bio)
{
	if(ssl!=NULL)
	{
		int r = SSL_shutdown(ssl);
		if(!r){
		  /* If we called SSL_shutdown() first then
			 we always get return value of '0'. In
			 this case, try again, but first send a
			 TCP FIN to trigger the other side's
			 close_notify*/
		  shutdown(fd,1);
		  r = SSL_shutdown(ssl);
		}
		SSL_free(ssl);
	}
	close(fd);
}
