/*
   BAREOS® - Backup Archiving REcovery Open Sourced

   Copyright (C) 2005-2009 Free Software Foundation Europe e.V.

   This program is Free Software; you can redistribute it and/or
   modify it under the terms of version two of the GNU Lesser General
   Public License as published by the Free Software Foundation plus
   additions in the file LICENSE.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   Lesser Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.
*/
/*
 * tls.h TLS support functions
 *
 * Author: Landon Fuller <landonf@threerings.net>
 */

#ifndef BAREOS_LIB_TLS_H_
#define BAREOS_LIB_TLS_H_

#if defined(HAVE_OPENSSL)
typedef struct TlsImplementationOpenSsl TLS_IMPLEMENTATION;
typedef struct TlsConnectionContextOpenSsl TLS_CONNECTION_CONTEXT;
#elif defined (HAVE_GNUTLS)
typedef struct TlsImplementationGnuTls TLS_IMPLEMENTATION;
typedef struct TlsConnectionContextGnuTls TLS_CONNECTION_CONTEXT;
#endif

#endif /* BAREOS_LIB_TLS_H_ */
