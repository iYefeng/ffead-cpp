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
 * ExtHandler.cpp
 *
 *  Created on: Jun 17, 2012
 *      Author: Sumeet
 */

#include "ExtHandler.h"

bool ExtHandler::handle(HttpRequest* req, HttpResponse* res, void* dlib, void* ddlib, const string& ext, Reflector& reflector)
{
	string& resourcePath = ConfigurationData::getInstance()->coreServerProperties.resourcePath;

	map<string, string>* tmplMap = NULL;
	if(ConfigurationData::getInstance()->templateMappingMap.find(req->getCntxt_name())!=ConfigurationData::getInstance()->templateMappingMap.end())
		tmplMap = &(ConfigurationData::getInstance()->templateMappingMap[req->getCntxt_name()]);

	map<string, string>* dcpMap = NULL;
	if(ConfigurationData::getInstance()->dcpMappingMap.find(req->getCntxt_name())!=ConfigurationData::getInstance()->dcpMappingMap.end())
		dcpMap = &(ConfigurationData::getInstance()->dcpMappingMap[req->getCntxt_name()]);

	map<string, string>* vwMap = NULL;
	if(ConfigurationData::getInstance()->viewMappingMap.find(req->getCntxt_name())!=ConfigurationData::getInstance()->viewMappingMap.end())
		vwMap = &(ConfigurationData::getInstance()->viewMappingMap[req->getCntxt_name()]);

	map<string, string>* ajaxIntfMap = NULL;
	if(ConfigurationData::getInstance()->ajaxInterfaceMap.find(req->getCntxt_name())!=ConfigurationData::getInstance()->ajaxInterfaceMap.end())
		ajaxIntfMap = &(ConfigurationData::getInstance()->ajaxInterfaceMap[req->getCntxt_name()]);

	map<string, string>* fviewMap = NULL;
	if(ConfigurationData::getInstance()->fviewMappingMap.find(req->getCntxt_name())!=ConfigurationData::getInstance()->fviewMappingMap.end())
		fviewMap = &(ConfigurationData::getInstance()->fviewMappingMap[req->getCntxt_name()]);

	Logger logger = LoggerFactory::getLogger("ExtHandler");
	bool cntrlit = false;
	string content, claz;

	string acurl = req->getActUrl();
	RegexUtil::replace(acurl,"[/]+","/");
	if(acurl.find("/"+req->getCntxt_name())!=0)
		acurl = "/" + req->getCntxt_name() + "/" + acurl;
	RegexUtil::replace(acurl,"[/]+","/");

	if(ajaxIntfMap!=NULL && ajaxIntfMap->find(acurl)!=ajaxIntfMap->end() && req->getMethod()=="POST" && req->getRequestParam("method")!="")
	{
		cntrlit = true;
		string& claz = ajaxIntfMap->find(acurl)->second;

		logger << "Inside Ajax Interface Execute" << endl;
		strVec vemp;
		string methName = req->getRequestParam("method");
		if(methName=="")
		{
			res->setHTTPResponseStatus(HTTPResponseStatus::InternalServerError);
		}
		else
		{
			string temp = req->getRequestParam("paramsize");
			int paramSize = 0;
			if(temp!="")
			{
				try {
					paramSize = CastUtil::lexical_cast<int>(temp.c_str());
				} catch(...) {
					res->setHTTPResponseStatus(HTTPResponseStatus::InternalServerError);
					paramSize = -1;
				}
			}
			if(paramSize>=0)
			{
				logger << "Reading Ajax params" << endl;
				for(int i=0;i<paramSize;i++)
				{
					stringstream s;
					string ss;
					s << (i+1);
					s >> ss;
					ss = "param_" + ss;
					//logger << ss << flush;
					string tem = req->getRequestParam(ss);
					vemp.push_back(tem);
				}
				string libName = INTER_LIB_FILE;
				string funcName;
				string metn,re;
				StringUtil::replaceAll(claz, "::", "_");
				metn = req->getCntxt_name() + "invokeAjaxMethodFor"+claz+methName;
				void *mkr = dlsym(dlib, metn.c_str());
				if(mkr!=NULL)
				{
					typedef string (*Funptr2) (strVec);
					Funptr2 f2 = (Funptr2)mkr;
					logger << ("Calling method " + metn) << endl;
					re = f2(vemp);
					logger << "Completed method call" << endl;
					res->setHTTPResponseStatus(HTTPResponseStatus::Ok);
					res->addHeaderValue(HttpResponse::ContentType, "text/plain");
					res->setContent(re);
				}
				else
				{
					res->setHTTPResponseStatus(HTTPResponseStatus::InternalServerError);
				}
			}
			else
			{
				res->setHTTPResponseStatus(HTTPResponseStatus::InternalServerError);
			}
		}
	}
	else if(ext==".form" && ConfigurationData::getInstance()->fviewFormMap.find(req->getCntxt_name())!=
			ConfigurationData::getInstance()->fviewFormMap.end())
	{
		cntrlit = FormHandler::handle(req, res, reflector);
		logger << ("Request handled by FormHandler") << endl;
	}
	else if(ext==".fview" && fviewMap!=NULL && fviewMap->find(req->getFile())!=fviewMap->end())
	{
		cntrlit = FviewHandler::handle(req, res);
		logger << ("Request handled by FviewHandler") << endl;
	}
#ifdef INC_DCP
	else if(dcpMap!=NULL && dcpMap->find(acurl)!=dcpMap->end())
	{
		string libName = INTER_LIB_FILE;
		if(ddlib != NULL)
		{
			cntrlit = true;
			string meth;
			string file = dcpMap->find(acurl)->second;
			meth = "_" + file + "emittHTML";

			void *mkr = dlsym(ddlib, meth.c_str());
			if(mkr!=NULL)
			{
				DCPPtr f =  (DCPPtr)mkr;
				content = f();
			}
			else
			{
				logger << ("No dcp found for " + meth) << endl;
			}
			if(content.length()>0)
			{
				res->setHTTPResponseStatus(HTTPResponseStatus::Ok);
				res->addHeaderValue(HttpResponse::ContentType, ContentTypes::CONTENT_TYPE_TEXT_SHTML);
				res->setContent(content);
			}
		}
	}
#endif
#ifdef INC_DVIEW
	else if(vwMap!=NULL && ext==".view" && vwMap->find(req->getFile())!=vwMap->end())
	{
		void *_temp = ConfigurationData::getInstance()->ffeadContext.getBean("dview_"+vwMap->find(req->getFile())->second, req->getCntxt_name());
		if(_temp!=NULL)
		{
			cntrlit = true;
			args argus;
			argus.push_back("Document*");
			vals valus;
			const ClassInfo& srv = ConfigurationData::getInstance()->ffeadContext.classInfoMap[req->getCntxt_name()][vwMap->find(req->getFile())->second];
			const Method& meth = srv.getMethod("getDocument", argus);
			if(meth.getMethodName()!="")
			{
				Document doc;
				valus.push_back(&doc);
				reflector.invokeMethodGVP(_temp,meth,valus);
				View view;
				string t = view.generateDocument(doc);
				content = t;
			}
			else
			{
				logger << "Invalid Dynamic View handler, no method getDocument found..." << endl;
			}
			ConfigurationData::getInstance()->ffeadContext.release("dview_"+vwMap->find(req->getFile())->second, req->getCntxt_name());
		}
		else
		{
			logger << "Invalid Dynamic View handler" << endl;
		}

		if(content.length()>0)
		{
			res->setHTTPResponseStatus(HTTPResponseStatus::Ok);
			res->addHeaderValue(HttpResponse::ContentType, ContentTypes::CONTENT_TYPE_TEXT_SHTML);
			res->setContent(content);
		}
	}
#endif
#ifdef INC_TPE
	else if(tmplMap!=NULL && tmplMap->find(acurl)!=tmplMap->end())
	{
		if(ddlib != NULL)
		{
			string tpefilename = tmplMap->find(acurl)->second.substr(tmplMap->find(acurl)->second.find(";")+1);
			string tpeclasname = tmplMap->find(acurl)->second.substr(0, tmplMap->find(acurl)->second.find(";"));
			cntrlit = true;
			void *_temp = ConfigurationData::getInstance()->ffeadContext.getBean("template_"+tpeclasname, req->getCntxt_name());
			if(_temp!=NULL)
			{
				args argus;
				vals valus;
				const ClassInfo& srv = ConfigurationData::getInstance()->ffeadContext.classInfoMap[req->getCntxt_name()][tpeclasname];
				argus.push_back("HttpRequest*");
				const Method& meth = srv.getMethod("getContext", argus);
				if(meth.getMethodName()!="")
				{
					valus.push_back(req);
					Context cnt = reflector.invokeMethod<Context>(_temp,meth,valus);
					logger << "Done with Template Context fetch" << endl;

					Context::iterator it;
					for (it=cnt.begin();it!=cnt.end();it++) {
						string key = it->first;
						logger << ("Template key=" + key + " Value = ") << it->second.getPointer() << endl;
					}

					string fname = "_" + tpefilename + "emittTemplateHTML";

					void* mkr = dlsym(ddlib, fname.c_str());
					if(mkr!=NULL)
					{
						TemplatePtr f =  (TemplatePtr)mkr;
						content = f(cnt);
					}
					else
					{
						logger << ("No template found for " + fname) << endl;
					}
				}
				else
				{
					logger << "Invalid Template handler, no method getContext found..." << endl;
				}
				ConfigurationData::getInstance()->ffeadContext.release("template_"+tpeclasname, req->getCntxt_name());
			}
			if(content.length()>0)
			{
				res->setHTTPResponseStatus(HTTPResponseStatus::Ok);
				res->addHeaderValue(HttpResponse::ContentType, ContentTypes::CONTENT_TYPE_TEXT_SHTML);
				res->setContent(content);
			}
		}
	}
#endif
	if(cntrlit && res->getStatusCode()!="404") {
		res->setDone(true);
	}
	return cntrlit;
}
