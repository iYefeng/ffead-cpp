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

// Apache specific header files
#include <httpd.h>
#include <http_config.h>
#include <http_protocol.h>
#include <http_main.h>
#include <http_request.h>
#include <apr_strings.h>
#include <http_core.h>
#include <http_log.h>
#include <apr_pools.h>
#include "util_script.h"
#include "ap_config.h"
#include "apr_strings.h"
#include "apr_general.h"
#include "util_filter.h"
#include "apr_buckets.h"
#include "HttpRequest.h"
#include "PropFileReader.h"
#include "cstdlib"
#include "dlfcn.h"
#include "WsUtil.h"
#include "sstream"
#include "StringUtil.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <queue>
#include "ComponentHandler.h"
#include "AppContext.h"
#include "Logger.h"
#include "ConfigurationHandler.h"
#include "ServiceTask.h"
#include "PropFileReader.h"
#include "XmlParseException.h"
#include "MessageHandler.h"
#include "MethodInvoc.h"
#undef strtoul
#ifdef WINDOWS
#include <direct.h>
#define pwd _getcwd
#else
#include <unistd.h>
#define pwd getcwd
#endif
#define MAXEPOLLSIZE 100
#define BACKLOG 500
#define MAXBUFLEN 1024

using namespace std;

static Logger logger;

extern "C" module AP_MODULE_DECLARE_DATA ffead_cpp_module;
static bool doneOnce = false;

typedef struct {
	const char* path;
	const char* defpath;
} ffead_cpp_module_config;

static ffead_cpp_module_config fconfig;

static void *create_modffeadcpp_config(apr_pool_t *p, server_rec *s)
{
	// This module's configuration structure.
	ffead_cpp_module_config *newcfg;

	// Allocate memory from the provided pool.
	newcfg = (ffead_cpp_module_config *) apr_pcalloc(p, sizeof(ffead_cpp_module_config));

	// Set the string to a default value.
	newcfg->path = "/";
	newcfg->defpath = "/";

	// Return the created configuration struct.
	return (void *) newcfg;
}

const char *set_modffeadcpp_path(cmd_parms *parms, void *mconfig, const char *arg)
{
	//ffead_cpp_module_config *s_cfg = (ffead_cpp_module_config*)ap_get_module_config(
	//		parms->server->module_config, &ffead_cpp_module);
	cout << "path = " << arg << endl;
	fconfig.path = arg;
	return NULL;
}

const char *set_modffeadcpp_defpath(cmd_parms *parms, void *mconfig, const char *arg)
{
	//ffead_cpp_module_config *s_cfg = (ffead_cpp_module_config*)ap_get_module_config(
	//		parms->server->module_config, &ffead_cpp_module);
	cout << "defpath = " << arg << endl;
	fconfig.defpath = arg;
	return NULL;
}

static const command_rec mod_ffeadcpp_cmds[] =
{
		AP_INIT_TAKE1(
				"FFEAD_CPP_PATH",
				set_modffeadcpp_path,
				NULL,
				RSRC_CONF,
				"FFEAD_CPP_PATH, the path to the ffead-server"
		),
		AP_INIT_TAKE1(
				"DocumentRoot",
				set_modffeadcpp_defpath,
				NULL,
				RSRC_CONF,
				"DocumentRoot"
		),
		{NULL}
};

static apr_bucket* get_file_bucket(request_rec* r, const char* fname)
{
	apr_file_t* file = NULL ;
	apr_finfo_t finfo ;
	if ( apr_stat(&finfo, fname, APR_FINFO_SIZE, r->pool) != APR_SUCCESS ) {
		return NULL ;
	}
	if ( apr_file_open(&file, fname, APR_READ|APR_SHARELOCK|APR_SENDFILE_ENABLED,
			APR_OS_DEFAULT, r->pool ) != APR_SUCCESS ) {
		return NULL ;
	}
	if ( ! file ) {
		return NULL ;
	}
	return apr_bucket_file_create(file, 0, finfo.size, r->pool, r->connection->bucket_alloc) ;
}

static int mod_ffeadcpp_method_handler (request_rec *r)
{
	string serverRootDirectory;
	serverRootDirectory.append(fconfig.path);

	string port = CastUtil::lexical_cast<string>(r->server->port);
	string content;

	apr_bucket_brigade *bb = apr_brigade_create(r->pool, r->connection->bucket_alloc);
	for ( ; ; ) {
		apr_bucket* b ;
		bool done = false;
		ap_get_brigade(r->input_filters, bb, AP_MODE_READBYTES, APR_BLOCK_READ, HUGE_STRING_LEN);
		for ( b = APR_BRIGADE_FIRST(bb);b != APR_BRIGADE_SENTINEL(bb);b = APR_BUCKET_NEXT(b) )
		{
			size_t bytes ;
			const char* buf = "\0";
			if ( APR_BUCKET_IS_EOS(b) )
			{
				done = true;
			}
			else if (apr_bucket_read(b, &buf, &bytes, APR_BLOCK_READ)== APR_SUCCESS )
			{
				content += string(buf, 0, bytes);
			}
		}

		if (done)
		  break;
		else
		  apr_brigade_cleanup(bb) ;
	}
	apr_brigade_destroy(bb) ;

	string cntpath = serverRootDirectory + "/web/";
	HttpRequest* req = new HttpRequest(cntpath);

	const apr_array_header_t* fields = apr_table_elts(r->headers_in);
	apr_table_entry_t* e = (apr_table_entry_t *) fields->elts;
	for(int i = 0; i < fields->nelts; i++) {
		req->buildRequest(e[i].key, e[i].val);
	}

	string ip_address = req->getHeader(HttpRequest::Host);
	string tipaddr = ip_address;
	if(port!="80")
		tipaddr += (":" + port);

	if(content!="")
	{
		req->buildRequest("Content", content.c_str());
	}
	req->buildRequest("URL", r->uri);
	req->buildRequest("Method", r->method);
	if(r->args != NULL && r->args[0] != '\0')
	{
		req->buildRequest("GetArguments", r->args);
	}
	req->buildRequest("HttpVersion", r->protocol);

	HttpResponse* respo = new HttpResponse;
	ServiceTask* task = new ServiceTask;
	task->handle(req, respo);
	delete task;

	for (int var = 0; var < (int)respo->getCookies().size(); var++)
	{
		apr_table_setn(r->headers_out, "Set-Cookie", respo->getCookies().at(var).c_str());
	}

	if(respo->isDone()) {
		string data = respo->generateResponse(false);
		map<string,string>::const_iterator it;
		for(it=respo->getHeaders().begin();it!=respo->getHeaders().end();it++) {
			if(StringUtil::toLowerCopy(it->first)==StringUtil::toLowerCopy(HttpResponse::ContentType)) {
				ap_set_content_type(r, it->second.c_str());
			} else {
				apr_table_setn(r->headers_out, it->first.c_str(), it->second.c_str());
			}
		}
		ap_rprintf(r, data.c_str(), data.length());
	} else {
		apr_file_t *file;
		apr_finfo_t finfo;
		int rc, exists;
		rc = apr_stat(&finfo, req->getUrl().c_str(), APR_FINFO_MIN, r->pool);
		if (rc == APR_SUCCESS) {
			exists =
			(
				(finfo.filetype != APR_NOFILE) &&  !(finfo.filetype & APR_DIR)
			);
			if (!exists) {
				delete respo;
				delete req;
				return HTTP_NOT_FOUND;
			}
		}
		else {
			delete respo;
			delete req;
			return HTTP_FORBIDDEN;
		}

		string webPath = string(fconfig.path) + "/web";
		RegexUtil::replace(webPath,"[/]+","/");
		string acurl = req->getUrl();
		RegexUtil::replace(acurl,"[/]+","/");
		if(acurl.find(webPath)==0) {
			acurl = acurl.substr(webPath.length());
		}
		RegexUtil::replace(acurl,"[/]+","/");
		logger << "static file will be processed by apache " << req->getUrl() << " " << acurl << endl;

		r->uri = acurl.c_str();
		r->finfo = finfo;
		r->filename = req->getUrl().c_str();
		apr_table_unset(r->headers_out, HttpResponse::Status.c_str());
		ap_set_content_type(r, CommonUtils::getMimeType(req->getExt()).c_str());
	}

	if(respo->isDone()) {
		delete respo;
		delete req;
		return DONE;
	} else {
		delete respo;
		delete req;
		return DECLINED;
	}
}


//Every module must declare it's data structure as shown above. Since this module does not require any configuration most of the callback locations have been left blank, except for the last one - that one is invoked by the HTTPD core so that the module can declare other functions that should be invoked to handle various events (like an HTTP request).

/*
 * This function is a callback and it declares what
 * other functions should be called for request
 * processing and configuration requests. This
 * callback function declares the Handlers for
 * other events.
 */

void one_time_init()
{
	string serverRootDirectory;
	serverRootDirectory.append(fconfig.path);
	//if(serverRootDirectory=="") {
	//	serverRootDirectory = fconfig.defpath;
	//}

    serverRootDirectory += "/";
	if(serverRootDirectory.find("//")==0)
	{
		RegexUtil::replace(serverRootDirectory,"[/]+","/");
	}

	string incpath = serverRootDirectory + "include/";
	string rtdcfpath = serverRootDirectory + "rtdcf/";
	string pubpath = serverRootDirectory + "public/";
	string respath = serverRootDirectory + "resources/";
	string webpath = serverRootDirectory + "web/";
	string logpath = serverRootDirectory + "logs/";
	string resourcePath = respath;

	PropFileReader pread;
	propMap srprps = pread.getProperties(respath+"server.prop");

	string servd = serverRootDirectory;
	string logp = respath+"/logging.xml";
	LoggerFactory::init(logp, serverRootDirectory, "", StringUtil::toLowerCopy(srprps["LOGGING_ENABLED"])=="true");

	logger = LoggerFactory::getLogger("MOD_FFEADCPP");

	bool isCompileEnabled = false;
   	string compileEnabled = srprps["DEV_MODE"];
	if(compileEnabled=="true" || compileEnabled=="TRUE")
		isCompileEnabled = true;

	/*if(srprps["SCRIPT_ERRS"]=="true" || srprps["SCRIPT_ERRS"]=="TRUE")
	{
		SCRIPT_EXEC_SHOW_ERRS = true;
	}*/
	bool sessatserv = true;
   	if(srprps["SESS_STATE"]=="server")
   		sessatserv = true;
   	long sessionTimeout = 3600;
   	if(srprps["SESS_TIME_OUT"]!="")
   	{
   		try {
   			sessionTimeout = CastUtil::lexical_cast<long>(srprps["SESS_TIME_OUT"]);
		} catch (...) {
			logger << "Invalid session timeout value defined, defaulting to 1hour/3600sec" << endl;
		}
   	}

	ConfigurationData::getInstance();
	SSLHandler::setIsSSL(false);

	strVec webdirs,webdirs1,pubfiles;
	//ConfigurationHandler::listi(webpath,"/",true,webdirs,false);
	CommonUtils::listFiles(webdirs, webpath, "/");
    //ConfigurationHandler::listi(webpath,"/",false,webdirs1,false);
	CommonUtils::listFiles(webdirs1, webpath, "/", false);

    CommonUtils::loadMimeTypes(respath+"mime-types.prop");
	CommonUtils::loadLocales(respath+"locale.prop");

	RegexUtil::replace(serverRootDirectory,"[/]+","/");
	RegexUtil::replace(webpath,"[/]+","/");

	CoreServerProperties csp(serverRootDirectory, respath, webpath, srprps, sessionTimeout, sessatserv);
	ConfigurationData::getInstance()->setCoreServerProperties(csp);

    strVec cmpnames;
    try
    {
    	ConfigurationHandler::handle(webdirs, webdirs1, incpath, rtdcfpath, serverRootDirectory, respath);
    }
    catch(const XmlParseException& p)
    {
    	logger << p.getMessage() << endl;
    }
    catch(const char* msg)
	{
		logger << msg << endl;
	}

    logger << INTER_LIB_FILE << endl;

    bool libpresent = true;
    void *dlibtemp = dlopen(INTER_LIB_FILE, RTLD_NOW);
	//logger << endl <<dlibtemp << endl;
	if(dlibtemp==NULL)
	{
		libpresent = false;
		logger << dlerror() << endl;
		logger.info("Could not load Library");
	}
	else
		dlclose(dlibtemp);

	//Generate library if dev mode = true or the library is not found in prod mode
	if(isCompileEnabled || !libpresent)
		libpresent = false;

	if(!libpresent)
	{
		string configureFilePath = rtdcfpath+"/autotools/configure";
		if (access( configureFilePath.c_str(), F_OK ) == -1 )
		{
			string compres = rtdcfpath+"/autotools/autogen.sh "+serverRootDirectory;
			string output = ScriptHandler::execute(compres, true);
			logger << "Set up configure for intermediate libraries\n\n" << endl;
		}

		if (access( configureFilePath.c_str(), F_OK ) != -1 )
		{
			string compres = respath+"rundyn-configure.sh "+serverRootDirectory;
		#ifdef DEBUG
			compres += " --enable-debug=yes";
		#endif
			string output = ScriptHandler::execute(compres, true);
			logger << "Set up makefiles for intermediate libraries\n\n" << endl;
			logger << output << endl;

			compres = respath+"rundyn-automake.sh "+serverRootDirectory;
			output = ScriptHandler::execute(compres, true);
			logger << "Intermediate code generation task\n\n" << endl;
			logger << output << endl;
		}
	}

	void* checkdlib = dlopen(INTER_LIB_FILE, RTLD_NOW);
	if(checkdlib==NULL)
	{
		string compres = rtdcfpath+"/autotools/autogen-noreconf.sh "+serverRootDirectory;
		string output = ScriptHandler::execute(compres, true);
		logger << "Set up configure for intermediate libraries\n\n" << endl;

		compres = respath+"rundyn-configure.sh "+serverRootDirectory;
		#ifdef DEBUG
			compres += " --enable-debug=yes";
		#endif
		output = ScriptHandler::execute(compres, true);
		logger << "Set up makefiles for intermediate libraries\n\n" << endl;
		logger << output << endl;

		compres = respath+"rundyn-automake.sh "+serverRootDirectory;
		if(!libpresent)
		{
			string output = ScriptHandler::execute(compres, true);
			logger << "Rerunning Intermediate code generation task\n\n" << endl;
			logger << output << endl;
		}
		checkdlib = dlopen(INTER_LIB_FILE, RTLD_NOW);
	}

	if(checkdlib==NULL)
	{
		logger << dlerror() << endl;
		logger.info("Could not load Library");
		exit(0);
	}
	else
	{
		dlclose(checkdlib);
		logger.info("Library generated successfully");
	}

#ifdef INC_COMP
	for (unsigned int var1 = 0;var1<ConfigurationData::getInstance()->componentNames.size();var1++)
	{
		string name = ConfigurationData::getInstance()->componentNames.at(var1);
		StringUtil::replaceFirst(name,"Component_","");
		ComponentHandler::registerComponent(name);
		AppContext::registerComponent(name);
	}
#endif

	bool distocache = false;
/*#ifdef INC_DSTC
	int distocachepoolsize = 20;
	try {
		if(srprps["DISTOCACHE_POOL_SIZE"]!="")
		{
			distocachepoolsize = CastUtil::lexical_cast<int>(srprps["DISTOCACHE_POOL_SIZE"]);
		}
	} catch(...) {
		logger << ("Invalid poolsize specified for distocache") << endl;
	}

	try {
		if(srprps["DISTOCACHE_PORT_NO"]!="")
		{
			CastUtil::lexical_cast<int>(srprps["DISTOCACHE_PORT_NO"]);
			DistoCacheHandler::trigger(srprps["DISTOCACHE_PORT_NO"], distocachepoolsize);
			logger << ("Session store is set to distocache store") << endl;
			distocache = true;
		}
	} catch(...) {
		logger << ("Invalid port specified for distocache") << endl;
	}

	if(!distocache) {
		logger << ("Session store is set to file store") << endl;
	}
#endif*/


#ifdef INC_JOBS
	JobScheduler::start();
#endif

	logger << ("Initializing WSDL files....") << endl;
	ConfigurationHandler::initializeWsdls();
	logger << ("Initializing WSDL files done....") << endl;

	void* dlib = dlopen(INTER_LIB_FILE, RTLD_NOW);
	//logger << endl <<dlib << endl;
	if(dlib==NULL)
	{
		logger << dlerror() << endl;
		logger.info("Could not load Library");
		exit(0);
	}
	else
	{
		logger.info("Library loaded successfully");
		dlclose(dlib);
	}

	void* ddlib = dlopen(DINTER_LIB_FILE, RTLD_NOW);
	//logger << endl <<dlib << endl;
	if(ddlib==NULL)
	{
		logger << dlerror() << endl;
		logger.info("Could not load dynamic Library");
		exit(0);
	}
	else
	{
		logger.info("Dynamic Library loaded successfully");
		dlclose(ddlib);
	}
}

/*
 * This routine is called to perform any module-specific fixing of header
 * fields, et cetera.  It is invoked just before any content-handler.
 *
 * The return value is OK, DECLINED, or HTTP_mumble.  If we return OK, the
 * server will still call any remaining modules with an handler for this
 * phase.
 */
static int mod_ffeadcp_post_config_hanlder(apr_pool_t *pconf, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s)
{
	/*void *data = NULL;
	const char *key = "dummy_post_config";

	// This code is used to prevent double initialization of the module during Apache startup
	apr_pool_userdata_get(&data, key, s->process->pool);
	if ( data == NULL ) {
	    apr_pool_userdata_set((const void *)1, key, apr_pool_cleanup_null, s->process->pool);
	    return OK;
	}*/

	// Get the module configuration
	//ffead_cpp_module_config *s_cfg = (ffead_cpp_module_config*)
	//		ap_get_module_config(s->module_config, &ffead_cpp_module);
	if(!doneOnce)
	{
		cout << "Configuring ffead-cpp....." << endl;
		one_time_init();
	}
	doneOnce = true;
    return OK;
}

static void mod_ffeadcp_child_uninit()
{
#ifdef INC_SDORM
	ConfigurationHandler::destroyDataSources();
#endif

	ConfigurationHandler::destroyCaches();

	ConfigurationData::getInstance()->clearAllSingletonBeans();
	return APR_SUCCESS;
}

static void mod_ffeadcp_child_init(apr_pool_t *p, server_rec *s)
{
	apr_pool_cleanup_register(p, NULL, mod_ffeadcp_child_uninit, apr_pool_cleanup_null);
	cout << "Initializing ffead-cpp....." << endl;
#ifdef INC_COMP
	try {
		if(srprps["CMP_PORT"]!="")
		{
			int port = CastUtil::lexical_cast<int>(srprps["CMP_PORT"]);
			if(port>0)
			{
				ComponentHandler::trigger(srprps["CMP_PORT"]);
			}
		}
	} catch(...) {
		logger << ("Component Handler Services are disabled") << endl;
	}
#endif

#ifdef INC_MSGH
	try {
		if(srprps["MESS_PORT"]!="")
		{
			int port = CastUtil::lexical_cast<int>(srprps["MESS_PORT"]);
			if(port>0)
			{
				MessageHandler::trigger(srprps["MESS_PORT"],resourcePath);
			}
		}
	} catch(...) {
		logger << ("Messaging Handler Services are disabled") << endl;
	}
#endif

#ifdef INC_MI
	try {
		if(srprps["MI_PORT"]!="")
		{
			int port = CastUtil::lexical_cast<int>(srprps["MI_PORT"]);
			if(port>0)
			{
				MethodInvoc::trigger(srprps["MI_PORT"]);
			}
		}
	} catch(...) {
		logger << ("Method Invoker Services are disabled") << endl;
	}
#endif

#ifdef INC_SDORM
	logger << ("Initializing DataSources....") << endl;
	ConfigurationHandler::initializeDataSources();
	logger << ("Initializing DataSources done....") << endl;
#endif

	logger << ("Initializing Caches....") << endl;
	ConfigurationHandler::initializeCaches();
	logger << ("Initializing Caches done....") << endl;

	//Load all the FFEADContext beans so that the same copy is shared by all process
	//We need singleton beans so only initialize singletons(controllers,authhandlers,formhandlers..)
	logger << ("Initializing ffeadContext....") << endl;
	ConfigurationData::getInstance()->initializeAllSingletonBeans();
	logger << ("Initializing ffeadContext done....") << endl;
}

static void mod_ffeadcpp_register_hooks (apr_pool_t *p)
{
	//ap_hook_insert_filter(myModuleInsertFilters, NULL, NULL, APR_HOOK_MIDDLE) ;
	//ap_register_input_filter(myInputFilterName, myInputFilter, NULL,AP_FTYPE_RESOURCE);
	/*ap_register_output_filter(myOutputFilterName, myOutputFilter, NULL, AP_FTYPE_RESOURCE) ;*/
	ap_hook_post_config(mod_ffeadcp_post_config_hanlder, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_child_init(mod_ffeadcp_child_init, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_handler(mod_ffeadcpp_method_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

extern "C"
{

//When this function is called by the HTTPD core, it registers a handler that should be invoked for all HTTP requests.

/*
 * This function is registered as a handler for HTTP methods and will
 * therefore be invoked for all GET requests (and others).  Regardless
 * of the request type, this function simply sends a message to
 * STDERR (which httpd redirects to logs/error_log).  A real module
 * would do *alot* more at this point.
 */



//Obviously an Apache module will require information about structures, macros and functions from Apache's core. These two header files are all that is required for this module, but real modules will need to include other header files relating to request handling, logging, protocols, etc.

/*
 * Declare and populate the module's data structure.  The
 * name of this structure ('ffead_cpp_module') is important - it
 * must match the name of the module.  This structure is the
 * only "glue" between the httpd core and the module.
 */
	module AP_MODULE_DECLARE_DATA ffead_cpp_module =
	{
			// Only one callback function is provided.  Real
			// modules will need to declare callback functions for
			// server/directory configuration, configuration merging
			// and other tasks.
			STANDARD20_MODULE_STUFF,
			NULL,
			NULL,
			NULL,//create_modffeadcpp_config,
			NULL,
			mod_ffeadcpp_cmds,
			mod_ffeadcpp_register_hooks,      /* callback for registering hooks */
	};
};

