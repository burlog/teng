/*
 * Teng -- a general purpose templating engine.
 * Copyright (C) 2004  Seznam.cz, a.s.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Seznam.cz, a.s.
 * Naskove 1, Praha 5, 15000, Czech Republic
 * http://www.seznam.cz, mailto:teng@firma.seznam.cz
 *
 *
 * $Id: teng.cc,v 1.4 2005-06-22 07:16:07 romanmarek Exp $
 *
 * DESCRIPTION
 * Teng engine -- implementation.
 *
 * AUTHORS
 * Vaclav Blazek <blazek@firma.seznam.cz>
 *
 * HISTORY
 * 2003-09-17  (vasek)
 *             Created.
 * 2005-06-21  (roman)
 *             Win32 support.
 */


#include <unistd.h>

#include <stdexcept>
#include <memory>

#include "teng.h"
#include "tengstructs.h"
#include "tengprocessor.h"
#include "tengcontenttype.h"
#include "tengtemplate.h"
#include "tengformatter.h"
#include "tengplatform.h"

using namespace std;

using namespace Teng;

extern "C" int teng_library_present() {
    return 0;
}

static int logErrors(const ContentType_t *contentType,
                     Writer_t &writer, Error_t &err)
{
    if (!err) return 0;
    bool useLineComment = false;
    if (contentType->blockComment.first.empty()) {
        useLineComment = true;
        if (!contentType->lineComment.empty())
            writer.write(contentType->lineComment + ' ');
    } else {
        if (!contentType->blockComment.first.empty())
            writer.write(contentType->blockComment.first + ' ');
    }
    writer.write("Error log:\n");
    const vector<Error_t::Entry_t> &errorLog = err.getEntries();
    for (vector<Error_t::Entry_t>::const_iterator
             ierrorLog = errorLog.begin();
         ierrorLog != errorLog.end(); ++ierrorLog) {
        if (useLineComment && !contentType->lineComment.empty())
            writer.write(contentType->lineComment + ' ');
        writer.write(ierrorLog->getLogLine());
    }
    if (!useLineComment)
        writer.write(contentType->blockComment.second + '\n');

    return 0;
}

Teng_t::Teng_t(const string &root, const Teng_t::Settings_t &settings)
    : root(root), templateCache(0), err()
{
    init(settings);
}

void Teng_t::init(const Settings_t &settings) {
    // if not absolute path, prepend current working directory
	if (root.empty() || !ISROOT(root)) {
        char cwd[2048];
        if (!getcwd(cwd, sizeof(cwd))) {
            Error_t::Position_t pos;
            err.logSyscallError(Error_t::LL_FATAL, pos, "Cannot get cwd");
            throw runtime_error("Cannot get cwd.");
        }
        root = string(cwd) + '/' + root;
    }

    // create template cache
    templateCache = new TemplateCache_t(root, settings.programCacheSize,
                                        settings.dictCacheSize);
}

Teng_t::~Teng_t() {
    delete templateCache;
}

namespace {
    string prependBeforeExt(const string &str, const string &prep) {
        // no prep or no str -> return str
        if (prep.empty() || str.empty()) return str;
        // find the last dot and the last slash
        string::size_type dot = str.rfind('.');
        string::size_type slash = str.rfind('/');
#ifdef WIN32
        string::size_type bslash = str.rfind('\\');
		if (bslash > slash)
			slash = bslash;
#endif //WIN32
        // if last slash exists and slash after dot or no dot
        // append prep at the end
        if (((slash != string::npos) && (slash > dot)) ||
            (dot == string::npos)) {
            return str + '.' + prep;
        } else {
            // else prepend prep before the last dot
            return str.substr(0, dot) + '.' + prep + str.substr(dot);
        }
    }
}

int Teng_t::generatePage(const string &templateFilename, const string &skin,
                         const string &_dict, const string &lang,
                         const string &param, const string &scontentType,
                         const string &encoding, const Fragment_t &data,
                         Writer_t &writer, Error_t &err)
{
    // find contentType desciptor for given contentType
    const ContentType_t *contentType
        = ContentType_t::findContentType(scontentType, err)->contentType;

    // make proper filename for language dictionary
    string langDictFilename = prependBeforeExt(_dict, lang);
    
    auto_ptr<Template_t>
        templ(templateCache->
              createTemplate(prependBeforeExt(templateFilename, skin),
                             langDictFilename, param,
                             TemplateCache_t::SRC_FILE));
    
    // append error logs of dicts and program
    err.append(templ->langDictionary->getErrors());
    err.append(templ->paramDictionary->getErrors());
    err.append(templ->program->getErrors());

    // if program is valid (not empty) execute it
    if (!templ->program->empty()) {
        Formatter_t output(writer);
        
        Processor_t(*templ->program, *templ->langDictionary,
                    *templ->paramDictionary, encoding,
                    contentType).run(data, output, err);
    }

    // log error into log, if said
    if (templ->paramDictionary->isLogToOutputEnabled())
        logErrors(contentType, writer, err);

    // flush writer to output
    writer.flush();

    // append writer errors
    err.append(writer.getErrors());

    // return error level from error log
    return err.getLevel();
}

int Teng_t::generatePage(const string &templateString,
                         const string &dict, const string &lang,
                         const string &param, const string &scontentType,
                         const string &encoding, const Fragment_t &data,
                         Writer_t &writer, Error_t &err)
{
    // find contentType desciptor for given contentType
    const ContentType_t *contentType
        = ContentType_t::findContentType(scontentType, err)->contentType;

    // make proper filename for language dictionary
    string langDictFilename = prependBeforeExt(dict, lang);
    
    auto_ptr<Template_t>templ(templateCache->createTemplate
                              (templateString, langDictFilename,
                               param, TemplateCache_t::SRC_STRING));
    
    // append error logs of dicts and program
    err.append(templ->langDictionary->getErrors());
    err.append(templ->paramDictionary->getErrors());
    err.append(templ->program->getErrors());
    
    // if program is valid (not empty) execute it
    if (!templ->program->empty()) {
        // create formatter for writer
        Formatter_t output(writer);

        // execute byte code
        Processor_t(*templ->program, *templ->langDictionary,
                    *templ->paramDictionary, encoding,
                    contentType).run(data, output, err);
    }

    // log error into log, if said
    if (templ->paramDictionary->isLogToOutputEnabled())
        logErrors(contentType, writer, err);

    // flush writer to output
    writer.flush();

    // append writer errors
    err.append(writer.getErrors());

    // return error level from error log
    return err.getLevel();
}

int Teng_t::dictionaryLookup(const string &config, const string &dict,
                             const string &lang, const string &key,
                             string &value)
{
    // find value for key
    const string *foundValue =
        templateCache->createDictionary
        (config, prependBeforeExt(dict, lang))-> lookup(key);
    if (!foundValue) {
        // not fount => error
        value.erase();
        return -1;
    }
    // found => assign
    value = *foundValue;
    // OK
    return 0;
}

void Teng_t::listSupportedContentTypes(vector<pair<string, string> > &supported)
{
    // retrieve supported content types
    ContentType_t::listSupported(supported);
}
