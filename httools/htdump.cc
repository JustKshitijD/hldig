//
// htdump.cc
//
// htdump: A utility to create ASCII text versions of the document
// and/or word databases. These can be used by external programs,
// edited, or used as a platform and version-independent form of the DB.
//
// Part of the ht://Dig package   <http://www.htdig.org/>
// Copyright (c) 1999-2000 The ht://Dig Group
// For copyright details, see the file COPYING in your distribution
// or the GNU Public License version 2 or later
// <http://www.gnu.org/copyleft/gpl.html>
//
// $Id: htdump.cc,v 1.1.2.1 2000/03/20 19:14:11 ghutchis Exp $
//

#include "HtURLCodec.h"
#include "HtWordList.h"
#include "HtConfiguration.h"
#include "DocumentDB.h"
#include "defaults.h"

#include <errno.h>
#include <unistd.h>

// If we have this, we probably want it.
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

int		verbose = 0;

void usage();
void reportError(char *msg);

//*****************************************************************************
// int main(int ac, char **av)
//
int main(int ac, char **av)
{
    int			do_words = 1;
    int			do_docs = 1;
    int			alt_work_area = 0;
    String		configfile = DEFAULT_CONFIG_FILE;
    int			c;
    extern char		*optarg;

    while ((c = getopt(ac, av, "vdwc:a")) != -1)
    {
	switch (c)
	{
	    case 'c':
		configfile = optarg;
		break;
	    case 'v':
		verbose++;
		break;
	    case 'a':
		alt_work_area++;
		break;
	    case 'w':
	        do_words = 0;
	        break;
	    case 'd':
	        do_docs = 0;
	        break;
	    case '?':
		usage();
		break;
	}
    }

    config.Defaults(&defaults[0]);

    if (access((char*)configfile, R_OK) < 0)
    {
	reportError(form("Unable to find configuration file '%s'",
			 configfile.get()));
    }
	
    config.Read(configfile);

    //
    // Check url_part_aliases and common_url_parts for
    // errors.
    String url_part_errors = HtURLCodec::instance()->ErrMsg();

    if (url_part_errors.length() != 0)
      reportError(form("Invalid url_part_aliases or common_url_parts: %s",
                       url_part_errors.get()));


    // We may need these through the methods we call
    if (alt_work_area != 0)
    {
	String	configValue;

	configValue = config["word_db"];
	if (configValue.length() != 0)
	{
	    configValue << ".work";
	    config.Add("word_db", configValue);
	}

	configValue = config["doc_db"];
	if (configValue.length() != 0)
	{
	    configValue << ".work";
	    config.Add("doc_db", configValue);
	}

	configValue = config["doc_index"];
	if (configValue.length() != 0)
	{
	    configValue << ".work";
	    config.Add("doc_index", configValue);
	}

	configValue = config["doc_excerpt"];
	if (configValue.length() != 0)
	{
	    configValue << ".work";
	    config.Add("doc_excerpt", configValue);
	}
    }

    if (do_docs)
      {
	const String doc_list = config["doc_list"];
	unlink(doc_list);
	DocumentDB docs;
	if (docs.Read(config["doc_db"], config["doc_index"], 
		      config["doc_excerpt"]) == OK)
	  {
	    docs.DumpDB(doc_list);
	  }
      }
    if (do_words)
      {
	const String word_dump = config["word_dump"];
	unlink(word_dump);
	HtWordList words(config);
	if(words.Open(config["word_db"], O_RDONLY) == OK) {
	  words.Dump(word_dump);
	}
      }

}


//*****************************************************************************
// void usage()
//   Display program usage information
//
void usage()
{
    cout << "usage: htdump [-v][-d][-w][-a][-c configfile]\n";
    cout << "This program is part of ht://Dig " << VERSION << "\n\n";
    cout << "Options:\n";
    cout << "\t-v\tVerbose mode.  This increases the verbosity of the\n";
    cout << "\t\tprogram.  Using more than 2 is probably only useful\n";
    cout << "\t\tfor debugging purposes.  The default verbose mode\n";
    cout << "\t\tgives a progress on what it is doing and where it is.\n\n";
    cout << "\t-d\tDo NOT dump the document database.\n\n";
    cout << "\t-w\tDo NOT dump the word database.\n\n";
    cout << "\t-a\tUse alternate work files.\n";
    cout << "\t\tTells htdump to append .work to the database files \n";
    cout << "\t\tallowing it to operate on a second set of databases.\n";
    cout << "\t-c configfile\n";
    cout << "\t\tUse the specified configuration file instead on the\n";
    cout << "\t\tdefault.\n\n";
    exit(0);
}


//*****************************************************************************
// Report an error and die
//
void reportError(char *msg)
{
    cout << "htdump: " << msg << "\n\n";
    exit(1);
}