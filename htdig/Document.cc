//
// Document.cc
//
// Document: This class holds everything there is to know about a document.
//           The actual contents of the document may or may not be present at
//           all times for memory conservation reasons.
//           The document can be told to retrieve its contents.  This is done
//           with the Retrieve call.  In case the retrieval causes a 
//           redirect, the link is followed, but this process is done 
//           only once (to prevent loops.) If the redirect didn't 
//           work, Document_not_found is returned.
//
// Part of the ht://Dig package   <http://www.htdig.org/>
// Copyright (c) 1995-2002 The ht://Dig Group
// For copyright details, see the file COPYING in your distribution
// or the GNU Public License version 2 or later
// <http://www.gnu.org/copyleft/gpl.html>
//
// $Id: Document.cc,v 1.58 2002/02/12 06:17:22 ghutchis Exp $
//

#ifdef HAVE_CONFIG_H
#include "htconfig.h"
#endif /* HAVE_CONFIG_H */

#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>

#include "Document.h"
#include "StringList.h"
#include "htdig.h"
#include "HTML.h"
#include "Plaintext.h"
#include "ExternalParser.h"
#include "lib.h"

#include "Transport.h"
#include "HtHTTP.h"

#ifdef HAVE_SSL_H
#include "HtHTTPSecure.h"
#endif

#include "HtHTTPBasic.h"
#include "ExternalTransport.h"

#include "defaults.h"

#if 1
typedef void (*SIGNAL_HANDLER) (...);
#else
typedef SIG_PF SIGNAL_HANDLER;
#endif

//*****************************************************************************
// Document::Document(char *u)
//   Initialize with the given url as the location for this document.
//   If the max_size is given, use that for size, otherwise use the
//   config value.
//
Document::Document(char *u, int max_size)
{
    url = 0;
    proxy = 0;
    referer = 0;
    contents = 0;
    transportConnect = 0;
    HTTPConnect = 0;
    HTTPSConnect = 0;
    FileConnect = 0;
    NNTPConnect = 0;
    externalConnect = 0;
	HtConfiguration* config= HtConfiguration::config();

    // We probably need to move assignment of max_doc_size, according
    // to a server or url configuration value. The same is valid for
    // max_retries.

    if (max_size > 0)
	max_doc_size = max_size;
    else
	max_doc_size = config->Value("max_doc_size");

   if (config->Value("max_retries") > 0)
      num_retries = config->Value("max_retries");
   else num_retries = 2;
   
   // Initialize some static variables of Transport

   Transport::SetDebugLevel(debug);

   // Initialize some static variables of Transport
   // and the User Agent for every HtHTTP objects

   HtHTTP::SetParsingController(ExternalParser::canParse);

   // Set the default parser content-type string
   Transport::SetDefaultParserContentType ("text/");

    contents.allocate(max_doc_size + 100);
    contentType = "";
    contentLength = -1;
    if (u)
    {
	Url(u);
    }
}


//*****************************************************************************
// Document::~Document()
//
Document::~Document()
{
   // We delete only the derived class objects
    if (HTTPConnect)
      delete HTTPConnect;
    if (HTTPSConnect)
      delete HTTPSConnect;
    if (FileConnect)
      delete FileConnect;
    if (NNTPConnect)
      delete NNTPConnect;
    if (externalConnect)
      delete externalConnect;
      
    if (url)
      delete url;
    if (proxy)
      delete proxy;
    if (referer)
      delete referer;

#if MEM_DEBUG
    char *p = new char;
    cout << "==== Document deleted: " << this << " new at " <<
	((void *) p) << endl;
    delete p;
#endif
}


//*****************************************************************************
// void Document::Reset()
//   Restore the Document object to an initial state.
//   We will not reset the authorization information since it can be reused.
//
void
Document::Reset()
{
    contentType = 0;
    contentLength = -1;
    if (url)
      delete url;
    url = 0;
    if (referer)
      delete referer;

    referer = 0;

    proxy=0;
    authorization=0;
    contents = 0;
    document_length = 0;
    redirected_to = 0;

}


//*****************************************************************************
// void Document::Url(const String &u)
//   Set the URL for this document
//
void
Document::Url(const String &u)
{
	HtConfiguration* config= HtConfiguration::config();
    if (url)
      delete url;
    url = new URL(u);

    const String proxyURL = config->Find(url,"http_proxy");
    if (proxyURL[0])
    {
	proxy = new URL(proxyURL);
	proxy->normalize();
    }

    const String credentials = config->Find(url,"authorization");
    if (credentials[0] )
	setUsernamePassword(credentials);
}


//*****************************************************************************
// void Document::Referer(const String &u)
//   Set the Referring URL for this document
//
void
Document::Referer(const String &u)
{
    if (referer)
      delete referer;
    referer = new URL(u);
}


//*****************************************************************************
// int Document::UseProxy()
//   Returns 1 if the given url is to be retrieved from the proxy server,
//   or 0 if it's not.
//
int
Document::UseProxy()
{
	HtConfiguration* config= HtConfiguration::config();
    static HtRegex *excludeProxy = 0;

    //
    // Initialize excludeProxy list if this is the first time.
    //
    if (!excludeProxy)
    {
    	excludeProxy = new HtRegex();
	StringList l(config->Find("http_proxy_exclude"), " \t");
	excludeProxy->setEscaped(l, config->Boolean("case_sensitive"));
	l.Release();
    }

    if ((proxy) && (excludeProxy->match(url->get(), 0, 0) == 0))
      return TRUE;    // if the exclude pattern is empty, use the proxy
    return FALSE;
}


//*****************************************************************************
// DocStatus Document::Retrieve(HtDateTime date)
//   Attempt to retrieve the document pointed to by our internal URL
//
Transport::DocStatus
Document::Retrieve(Server *server, HtDateTime date)
{
   // Right now we just handle http:// service
   // Soon this will include file://
   // as well as an ExternalTransport system
   // eventually maybe ftp:// and a few others

   Transport::DocStatus	status;
   Transport_Response	*response = 0;
   HtDateTime 		*ptrdatetime = 0;
   int			useproxy = UseProxy();
   int                  NumRetries;
  
   transportConnect = 0;

   if (ExternalTransport::canHandle(url->service()))
     {
       if (externalConnect)
	 {
	   delete externalConnect;
        }	
       externalConnect = new ExternalTransport(url->service());
       transportConnect = externalConnect;
     }
#ifdef HAVE_SSL_H
   else if (mystrncasecmp(url->service(), "https", 5) == 0)
   {
      if (!HTTPSConnect)
      {
         if (debug>4)
            cout << "Creating an HtHTTPSecure object" << endl;
      
         HTTPSConnect = new HtHTTPSecure();

         if (!HTTPSConnect)
               return Transport::Document_other_error;
      }
      
      if (HTTPSConnect)
      {
         // Here we must set only thing for a HTTP request
	  
         HTTPSConnect->SetRequestURL(*url);

	 // Set the user agent which can vary per server
	 HTTPSConnect->SetRequestUserAgent(server->UserAgent());

	 // Set the accept language which can vary per server
	 HTTPSConnect->SetAcceptLanguage(server->AcceptLanguage());

         // Set the referer
         if (referer)
            HTTPSConnect->SetRefererURL(*referer);
	  
	  // We may issue a config paramater to enable/disable them
         if (server->IsPersistentConnectionAllowed())
         {
            // Persistent connections allowed
            HTTPSConnect->AllowPersistentConnection();

            // Head before Get option control
            if (server->HeadBeforeGet())
               HTTPSConnect->EnableHeadBeforeGet();
            else
               HTTPSConnect->DisableHeadBeforeGet();
         }
         else HTTPSConnect->DisablePersistentConnection();

	  // http->SetRequestMethod(HtHTTP::Method_GET);
         if (debug > 2)
         {
            cout << "Making HTTPS request on " << url->get();
	      
            if (useproxy)
               cout << " via proxy (" << proxy->host() << ":" << proxy->port() << ")";
	      
            cout << endl;
         }
      }

      HTTPSConnect->SetProxy(useproxy);
      transportConnect = HTTPSConnect;
   }
#endif
   else if (mystrncasecmp(url->service(), "http", 4) == 0)
   {
      if (!HTTPConnect)
      {
         if (debug>4)
            cout << "Creating an HtHTTPBasic object" << endl;
      
         HTTPConnect = new HtHTTPBasic();

         if (!HTTPConnect)
               return Transport::Document_other_error;
      }
      
      if (HTTPConnect)
      {
         // Here we must set only thing for a HTTP request
	  
         HTTPConnect->SetRequestURL(*url);

	 // Set the user agent which can vary per server
	 HTTPConnect->SetRequestUserAgent(server->UserAgent());

	 // Set the accept language which can vary per server
	 HTTPConnect->SetAcceptLanguage(server->AcceptLanguage());

         // Set the referer
         if (referer)
            HTTPConnect->SetRefererURL(*referer);
	  
         // Let's disable the cookies if we decided that in the config file 
         if (server->DisableCookies())
            HTTPConnect->DisableCookies();
         else HTTPConnect->AllowCookies();

	  // We may issue a config paramater to enable/disable them
         if (server->IsPersistentConnectionAllowed())
         {
            // Persistent connections allowed
            HTTPConnect->AllowPersistentConnection();

            // Head before Get option control
            if (server->HeadBeforeGet())
               HTTPConnect->EnableHeadBeforeGet();
            else
               HTTPConnect->DisableHeadBeforeGet();
         }
         else HTTPConnect->DisablePersistentConnection();

	  // http->SetRequestMethod(HtHTTP::Method_GET);
         if (debug > 2)
         {
            cout << "Making HTTP request on " << url->get();
	      
            if (useproxy)
               cout << " via proxy (" << proxy->host() << ":" << proxy->port() << ")";
	      
            cout << endl;
         }
      }

      HTTPConnect->SetProxy(useproxy);
      transportConnect = HTTPConnect;
   }
   else if (mystrncasecmp(url->service(), "file", 4) == 0)
   {
      if (!FileConnect)
      {
         if (debug>4)
            cout << "Creating an HtFile object" << endl;

         FileConnect = new HtFile();

         if (!FileConnect)
               return Transport::Document_other_error;
      }

      if (FileConnect)
      {
         // Here we must set only thing for a file request
	
         FileConnect->SetRequestURL(*url);
	
         // Set the referer
         if (referer)
            FileConnect->SetRefererURL(*referer);
	
         if (debug > 2)
            cout << "Making 'file' request on " << url->get() << endl;
      }

      transportConnect = FileConnect;
   }
   else if (mystrncasecmp(url->service(), "news", 4) == 0)
   {
      if (!NNTPConnect)
      {
         if (debug>4)
            cout << "Creating an HtNNTP object" << endl;

         NNTPConnect = new HtNNTP();

         if (!NNTPConnect)
               return Transport::Document_other_error;
      }

      if (NNTPConnect)
      {
         // Here we got an Usenet document request
	
         NNTPConnect->SetRequestURL(*url);
	
         if (debug > 2)
            cout << "Making 'NNTP' request on " << url->get() << endl;
      }

      transportConnect = NNTPConnect;
   }
   else
   {
      if (debug)
      {
         cout << '"' << url->service() <<
            "\" not a recognized transport service. Ignoring\n";
      }
      
      return Transport::Document_not_recognized_service;
   }
  
   // Is a transport object pointer available?
  
   if (transportConnect)
   {
      // Set all the appropriate parameters
      if (useproxy)
         transportConnect->SetConnection(proxy);
      else
         transportConnect->SetConnection(url);
      
      // OK. Let's set the connection time out
      transportConnect->SetTimeOut(server->TimeOut());
      
      // Let's set number of retries for a failed connection attempt
      transportConnect->SetRetry(server->TcpMaxRetries());
      
      // ... And the wait time after a failure
      transportConnect->SetWaitTime(server->TcpWaitTime());
      
      // OK. Let's set the maximum size of a document to be retrieved
      transportConnect->SetRequestMaxDocumentSize(max_doc_size);
      
      // Let's set the credentials
      if (authorization.length())
      	transportConnect->SetCredentials(authorization);
      
      // Let's set the modification time (in order not to retrieve a
      // document we already have)
      transportConnect->SetRequestModificationTime(date);
      
      // Make the request
      // Here is the main operation ... Let's make the request !!!
      // We now perform a loop until we want to retry the request

      NumRetries = 0;
      
      do
      {
         status = transportConnect->Request();

         if (NumRetries++)
            if(debug>0)
               cout << ".";

      } while (ShouldWeRetry(status) && NumRetries < num_retries);
      
            
      // Let's get out the info we need
      response = transportConnect->GetResponse();
      
      if (response)
      {
         // We got the response
	  
         contents = response->GetContents();
         contentType = response->GetContentType();
         contentLength = response->GetContentLength();
         ptrdatetime = response->GetModificationTime();
         document_length = response->GetDocumentLength();

         if (transportConnect == HTTPConnect || transportConnect == externalConnect)
            redirected_to =  ((HtHTTP_Response *)response)->GetLocation();

         if (ptrdatetime)
         {
            // We got the modification date/time
            modtime = *ptrdatetime;
         }

         // How to manage it when there's no modification date/time?
	  
         if (debug > 5)
         {
            cout << "Contents:\n" << contents << endl;
            cout << "Content Type: " << contentType << endl;
            cout << "Content Length: " << contentLength << endl;
            cout << "Modification Time: " << modtime.GetISO8601() << endl;
         }
      }
      
      return status;
      
   }
   else
      return Transport::Document_not_found;
}
  
//*****************************************************************************
// DocStatus Document::RetrieveLocal(HtDateTime date, StringList *filenames)
//   Attempt to retrieve the document pointed to by our internal URL
//   using a list of potential local filenames given. Returns Document_ok,
//   Document_not_changed or Document_not_local (in which case the
//   retriever tries it again using the standard retrieve method).
//
Transport::DocStatus
Document::RetrieveLocal(HtDateTime date, StringList *filenames)
{
	HtConfiguration* config= HtConfiguration::config();
    struct stat stat_buf;
    String *filename;

    filenames->Start_Get();

    // Loop through list of potential filenames until the list is exhausted
    // or a suitable file is found to exist as a regular file.
    while ((filename = (String *)filenames->Get_Next()) &&
	   ((stat((char*)*filename, &stat_buf) == -1) || !S_ISREG(stat_buf.st_mode)))
        if (debug > 1)
	    cout << "  tried local file " << *filename << endl;
    
    if (!filename)
        return Transport::Document_not_local;

    if (debug > 1)
        cout << "  found existing file " << *filename << endl;

    modtime = stat_buf.st_mtime;
    if (modtime <= date)
      return Transport::Document_not_changed;

    // Process only HTML files (this could be changed if we read
    // the server's mime.types file).
    // (...and handle a select few other types for now...  this should
    //  eventually be handled by the "file://..." handler, which uses
    //  mime.types to determine the file type.) -- FIXME!!
    char *ext = strrchr((char*)*filename, '.');
    if (ext == NULL)
      return Transport::Document_not_local;
    if ((mystrcasecmp(ext, ".html") == 0) || (mystrcasecmp(ext, ".htm") == 0))
        contentType = "text/html";
    else if ((mystrcasecmp(ext, ".txt") == 0) || (mystrcasecmp(ext, ".asc") == 0))
        contentType = "text/plain";
    else if ((mystrcasecmp(ext, ".pdf") == 0))
        contentType = "application/pdf";
    else if ((mystrcasecmp(ext, ".ps") == 0) || (mystrcasecmp(ext, ".eps") == 0))
        contentType = "application/postscript";
    else 
      return Transport::Document_not_local;

    // Open it
    FILE *f = fopen((char*)*filename, "r");
    if (f == NULL)
      return Transport::Document_not_local;

    //
    // Read in the document itself
    //
    max_doc_size = config->Value(url,"max_doc_size");
    contents = 0;
    char	docBuffer[8192];
    int		bytesRead;

    while ((bytesRead = fread(docBuffer, 1, sizeof(docBuffer), f)) > 0)
    {
	if (debug > 2)
	    cout << "Read " << bytesRead << " from document\n";
	if (contents.length() + bytesRead > max_doc_size)
	    bytesRead = max_doc_size - contents.length();
	contents.append(docBuffer, bytesRead);
	if (contents.length() >= max_doc_size)
	    break;
    }
    fclose(f);
    document_length = contents.length();
    contentLength = stat_buf.st_size;

    if (debug > 2)
	cout << "Read a total of " << document_length << " bytes\n";

    if (document_length < contentLength)
      document_length = contentLength;
    return Transport::Document_ok;
}


//*****************************************************************************
// Parsable *Document::getParsable()
//   Given the content-type of a document, returns a document parser.
//   This will first look through the list of user supplied parsers and
//   then at our (limited) builtin list of parsers.  The user supplied
//   parsers are external programs that will be used.
//
Parsable *
Document::getParsable()
{
    static HTML			*html = 0;
    static Plaintext		*plaintext = 0;
    static ExternalParser	*externalParser = 0;
    
    Parsable	*parsable = 0;

    if (ExternalParser::canParse(contentType))
    {
	if (externalParser)
	{
	    delete externalParser;
	}
	externalParser = new ExternalParser(contentType);
	parsable = externalParser;
    }
    else if (mystrncasecmp((char*)contentType, "text/html", 9) == 0)
    {
	if (!html)
	    html = new HTML();
	parsable = html;
    }
    else if (mystrncasecmp((char*)contentType, "text/plain", 10) == 0)
    {
	if (!plaintext)
	    plaintext = new Plaintext();
	parsable = plaintext;
    }
    else if (mystrncasecmp((char *)contentType, "text/css", 8) == 0)
      {
	return NULL;
      }
    else if (mystrncasecmp((char *)contentType, "text/", 5) == 0)
    {
	if (!plaintext)
	    plaintext = new Plaintext();
	parsable = plaintext;
	if (debug > 1)
	{
	    cout << '"' << contentType <<
		"\" not a recognized type.  Assuming text/plain\n";
	}
    }
    else
      {
	if (debug > 1)
	{
	    cout << '"' << contentType <<
		"\" not a recognized type.  Ignoring\n";
	}
	return NULL;
      }

    parsable->setContents(contents.get(), contents.length());
    return parsable;
}


int Document::ShouldWeRetry(Transport::DocStatus DocumentStatus)
{

   if (DocumentStatus == Transport::Document_connection_down)
      return 1;
      
   if (DocumentStatus == Transport::Document_no_connection)
      return 1;
      
   if (DocumentStatus == Transport::Document_no_header)
      return 1;
      
   return 0;
}
