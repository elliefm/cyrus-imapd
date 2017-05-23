/* xstreq.h -- string equality tests
 *
 * Copyright (c) 1994-2017 Carnegie Mellon University.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name "Carnegie Mellon University" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For permission or any legal
 *    details, please contact
 *      Carnegie Mellon University
 *      Center for Technology Transfer and Enterprise Creation
 *      4615 Forbes Avenue
 *      Suite 302
 *      Pittsburgh, PA  15213
 *      (412) 268-7393, fax: (412) 268-7395
 *      innovation@andrew.cmu.edu
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Computing Services
 *     at Carnegie Mellon University (http://www.cmu.edu/computing/)."
 *
 * CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef INCLUDED_XSTREQ_H
#define INCLUDED_XSTREQ_H

#include <config.h>

#include <string.h>
#include <strings.h>

#include "lib/util.h"

/*
 * string equality macros: true if the strings are equal, false otherwise.
 *
 * strcmp() is great for sort algorithms, but in most cases all we care about
 * is whether or not the strings are equal.  thus: a sane interface for that.
 */

#define xstreq(a,b)         (strcmp(a,b) == 0)
#define xstrneq(a,b,n)      (strncmp(a,b,n) == 0)

#define xstrcaseeq(a,b)     (strcasecmp(a,b) == 0)
#define xstrncaseeq(a,b,n)  (strncasecmp(a,b,n) == 0)

#define xstreqsafe(a,b)     (strcmpsafe(a,b) == 0)
#define xstrcaseeqsafe(a,b) (strcasecmpsafe(a,b) == 0)
#define xstrneqsafe(a,b)    (strncmpsafe(a,b) == 0)
#define xstreqnull(a,b)     (strcmpnull(a,b) == 0)

#endif
