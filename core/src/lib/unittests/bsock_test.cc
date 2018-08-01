/*
   BAREOS® - Backup Archiving REcovery Open Sourced

   Copyright (C) 2018-2018 Bareos GmbH & Co. KG

   This program is Free Software; you can redistribute it and/or
   modify it under the terms of version three of the GNU Affero General Public
   License as published by the Free Software Foundation and included
   in the file LICENSE.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.
*/
#include "gtest/gtest.h"
#include <iostream>
#include <fstream>

#include <thread>
#include <future>

#include "include/bareos.h"
#include "console/console_conf.h"

#include "include/jcr.h"

class UaContext {
  BareosSocket *UA_sock;
};
class StorageResource;
#include "dird/authenticate.h"

#define MIN_MSG_LEN 15
#define MAX_MSG_LEN 175 + 25
#define PORT 54321
#define HOST "127.0.0.1"
#define CONSOLENAME "testconsole"
#define CONSOLEPASSWORD "secret"

static void InitForTest()
{
  setlocale(LC_ALL, "");
  bindtextdomain("bareos", LOCALEDIR);
  textdomain("bareos");

  debug_level = 0;
  SetTrace(0);

  working_directory = "/tmp";
  MyNameIs(0, NULL, "bsock_test");
  InitMsg(NULL, NULL);
}

int create_accepted_server_socket(int port)
{
  int server_fd, new_socket;
  struct sockaddr_in address;
  int opt = 1;
  int addrlen = sizeof(address);

  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }
  if (listen(server_fd, 3) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }
  if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
    perror("accept");
    exit(EXIT_FAILURE);
  }
  return new_socket;
}

void start_bareos_server(std::promise<bool> *promise, std::string console_name,
                         std::string console_password, std::string server_address, int server_port,
                         ConsoleResource *cons)

{
  int newsockfd = create_accepted_server_socket(server_port);
  bool retval = false;

  BareosSocket *bs;
  bs = New(BareosSocketTCP);
  bs->sleep_time_after_authentication_error = 0;
  bs->fd_ = newsockfd;
  bs->SetWho(bstrdup("client"));
  memset(&bs->peer_addr, 0, sizeof(bs->peer_addr));

  char *name = (char *)console_name.c_str();
  s_password *password = new (s_password);
  password->encoding = p_encoding_md5;
  password->value = (char *)console_password.c_str();

  if (bs->recv() <= 0) {
    Dmsg1(10, _("Connection request from %s failed.\n"), bs->who());
    // Bmicrosleep(5, 0); /* make user wait 5 seconds */
    bs->close();
  }
  // Do a sanity check on the message received
  if (bs->message_length < MIN_MSG_LEN || bs->message_length > MAX_MSG_LEN) {
    Dmsg2(10, _("Invalid connection from %s. Len=%d\n"), bs->who(), bs->message_length);
    // Bmicrosleep(5, 0); /* make user wait 5 seconds */
    bs->close();
  }
  bs->local_daemon_type_ = BareosDaemonType::kDirector;
  bs->remote_daemon_type_ = BareosDaemonType::kConsole;
  Dmsg1(10, "Cons->Dir: %s", bs->msg);
  if (bs->AuthenticateInboundConnection(NULL, "Console", name, *password, cons)) {
    bs->fsend(_("1000 OK: %s Version: %s (%s)\n"), my_name, VERSION, BDATE);
    Dmsg0(10, "Server: inbound auth successful\n");
    retval = true;
  } else {
    Dmsg0(10, "Server: inbound auth failed\n");
  }
  promise->set_value(retval);
  bs->close();
}

int connect_to_server(std::string console_name, std::string console_password,
                      std::string server_address, int server_port, DirectorResource *dir)
{
  utime_t heart_beat = 0;
  char errmsg[1024];
  int errmsg_len = sizeof(errmsg);

  JobControlRecord jcr;
  memset(&jcr, 0, sizeof(jcr));

  char *name = (char *)console_name.c_str();

  s_password *password = new (s_password);
  password->encoding = p_encoding_md5;
  password->value = (char *)console_password.c_str();

  BareosSocketTCP *UA_sock = New(BareosSocketTCP);
  UA_sock->sleep_time_after_authentication_error = 0;
  jcr.dir_bsock = UA_sock;
  UA_sock->local_daemon_type_ = BareosDaemonType::kConsole;
  UA_sock->remote_daemon_type_ = BareosDaemonType::kDirector;

  if (!UA_sock->connect(NULL, 1, 15, heart_beat, "Director daemon", (char *)server_address.c_str(),
                        NULL, server_port, false)) {
    Dmsg0(10, "socket connect failed\n");
    delete UA_sock;
    return false;

  } else {
    Dmsg0(10, "socket connect OK\n");

    if (!UA_sock->AuthenticateWithDirector(&jcr, name, *password, errmsg, errmsg_len, dir)) {
      Emsg0(M_ERROR, 0, "Authenticate Failed\n");
      UA_sock->close();
      return false;
    } else {
      UA_sock->close();
      Dmsg0(10, "Authenticate Connect to Server successful!\n");
      if (dir->tls_psk.enable) {
         Dmsg0(10, UA_sock->TlsEstablished() ? "Tls enabled\n" : "Tls failed to establish\n");
         return UA_sock->TlsEstablished();
      } else {
         Dmsg0(10, "Tls disabled by command\n");
         if (UA_sock->TlsEstablished()) {
            Dmsg0(10, "UA_sock->tls_established_ should be false but is true\n");
         }
         return !UA_sock->TlsEstablished();
      }
    }
  }
}

ConsoleResource *CreateAndInitializeNewConsoleResource()
{
  ConsoleResource *cons = new (ConsoleResource);
  cons->tls_psk.enable = false;  // enable_tls_psk;
  cons->tls_cert.certfile = new (std::string)(CERTDIR "/console.bareos.org-cert.pem");
  cons->tls_cert.keyfile = new (std::string)(CERTDIR "/console.bareos.org-key.pem");
  cons->tls_cert.CaCertfile = new (std::string)(CERTDIR "/bareos-ca.pem");
  cons->tls_cert.enable = false;
  cons->tls_cert.VerifyPeer = false;
  cons->tls_cert.require = false;
  return cons;
}

DirectorResource *CreateAndInitializeNewDirectorResource()
{
  DirectorResource *dir = new (DirectorResource);
  dir->address = (char *)HOST;
  dir->DIRport = htons(PORT);
  dir->tls_psk.enable = false;
  dir->tls_cert.certfile = new (std::string)(CERTDIR "/bareos-dir.bareos.org-cert.pem");
  dir->tls_cert.keyfile = new (std::string)(CERTDIR "/bareos-dir.bareos.org-key.pem");
  dir->tls_cert.CaCertfile = new (std::string)(CERTDIR "/bareos-ca.pem");
  dir->tls_cert.enable = false;
  dir->tls_cert.VerifyPeer = false;
  dir->tls_cert.require = false;
  return dir;
}

std::string client_cons_name;
std::string client_cons_password;

std::string server_cons_name;
std::string server_cons_password;

int port = PORT;

TEST(bsock, auth_works)
{
  port++;
  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();

  client_cons_name = "clientname";
  client_cons_password = "verysecretpassword";

  server_cons_name = client_cons_name;
  server_cons_password = client_cons_password;

  ConsoleResource *cons = CreateAndInitializeNewConsoleResource();
  DirectorResource *dir = CreateAndInitializeNewDirectorResource();

  InitForTest();

  dir->tls_psk.enable = false;
  cons->tls_psk.enable = false;

  Dmsg0(10, "starting listen thread...\n");
  std::thread server_thread(start_bareos_server, &promise, server_cons_name, server_cons_password,
                            HOST, port, cons);

  Dmsg0(10, "connecting to server\n");
  ASSERT_TRUE(connect_to_server(client_cons_name, client_cons_password, HOST, port, dir));

  server_thread.join();
  ASSERT_TRUE(future.get());
}


TEST(bsock, auth_works_with_different_names)
{
  port++;
  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();

  client_cons_name = "clientname";
  client_cons_password = "verysecretpassword";

  server_cons_name = "differentclientname";
  server_cons_password = client_cons_password;

  ConsoleResource *cons = CreateAndInitializeNewConsoleResource();
  DirectorResource *dir = CreateAndInitializeNewDirectorResource();

  InitForTest();

  dir->tls_psk.enable = false;
  cons->tls_psk.enable = false;

  Dmsg0(10, "starting listen thread...\n");
  std::thread server_thread(start_bareos_server, &promise, server_cons_name, server_cons_password,
                            HOST, port, cons);

  Dmsg0(10, "connecting to server\n");
  ASSERT_TRUE(connect_to_server(client_cons_name, client_cons_password, HOST, port, dir));

  server_thread.join();
  ASSERT_TRUE(future.get());
}


TEST(bsock, auth_fails_with_different_passwords)
{
  port++;
  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();

  client_cons_name = "clientname";
  client_cons_password = "verysecretpassword";

  server_cons_name = client_cons_name;
  server_cons_password = "othersecretpassword";

  ConsoleResource *cons = CreateAndInitializeNewConsoleResource();
  DirectorResource *dir = CreateAndInitializeNewDirectorResource();

  InitForTest();

  dir->tls_psk.enable = false;
  cons->tls_psk.enable = false;

  Dmsg0(10, "starting listen thread...\n");
  std::thread server_thread(start_bareos_server, &promise, server_cons_name, server_cons_password,
                            HOST, port, cons);

  Dmsg0(10, "connecting to server\n");
  ASSERT_FALSE(connect_to_server(client_cons_name, client_cons_password, HOST, port, dir));

  server_thread.join();
  ASSERT_FALSE(future.get());
}

TEST(bsock, auth_works_with_tls_psk)
{
  port++;
  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();

  client_cons_name = "clientname";
  client_cons_password = "verysecretpassword";

  server_cons_name = client_cons_name;
  server_cons_password = client_cons_password;

  ConsoleResource *cons = CreateAndInitializeNewConsoleResource();
  DirectorResource *dir = CreateAndInitializeNewDirectorResource();

  InitForTest();

  dir->tls_psk.enable = true;
  cons->tls_psk.enable = true;

  Dmsg0(10, "starting listen thread...\n");
  std::thread server_thread(start_bareos_server, &promise, server_cons_name, server_cons_password,
                            HOST, port, cons);

  Dmsg0(10, "connecting to server\n");
  ASSERT_TRUE(connect_to_server(client_cons_name, client_cons_password, HOST, port, dir));

  server_thread.join();
  ASSERT_TRUE(future.get());

}


TEST(bsock, auth_fails_with_different_names_with_tls_psk)
{
  port++;
  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();

  client_cons_name = "clientname";
  client_cons_password = "verysecretpassword";

  server_cons_name = "differentclientname";
  server_cons_password = client_cons_password;

  ConsoleResource *cons = CreateAndInitializeNewConsoleResource();
  DirectorResource *dir = CreateAndInitializeNewDirectorResource();

  InitForTest();

  dir->tls_psk.enable = true;
  cons->tls_psk.enable = true;

  Dmsg0(10, "starting listen thread...\n");
  std::thread server_thread(start_bareos_server, &promise, server_cons_name, server_cons_password,
                            HOST, port, cons);

  Dmsg0(10, "connecting to server\n");
  ASSERT_FALSE(connect_to_server(client_cons_name, client_cons_password, HOST, port, dir));

  server_thread.join();
  ASSERT_FALSE(future.get());
}

TEST(bsock, auth_works_with_tls_cert)
{
  port++;
  std::promise<bool> promise;
  std::future<bool> future = promise.get_future();

  client_cons_name = "clientname";
  client_cons_password = "verysecretpassword";

  server_cons_name = client_cons_name;
  server_cons_password = client_cons_password;

  ConsoleResource *cons = CreateAndInitializeNewConsoleResource();
  DirectorResource *dir = CreateAndInitializeNewDirectorResource();

  InitForTest();

  dir->tls_psk.enable = false;
  cons->tls_psk.enable = false;

  dir->tls_cert.enable = true;
  cons->tls_cert.enable = true;


  Dmsg0(10, "starting listen thread...\n");
  std::thread server_thread(start_bareos_server, &promise, server_cons_name, server_cons_password,
                            HOST, port, cons);

  Dmsg0(10, "connecting to server\n");
  ASSERT_TRUE(connect_to_server(client_cons_name, client_cons_password, HOST, port, dir));

  server_thread.join();
  ASSERT_TRUE(future.get());
}
