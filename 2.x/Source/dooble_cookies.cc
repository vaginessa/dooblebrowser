/*
** Copyright (c) 2008 - present, Alexis Megas.
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from Dooble without specific prior written permission.
**
** DOOBLE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** DOOBLE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <QDir>
#include <QNetworkCookie>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>

#include "dooble.h"
#include "dooble_cookies.h"
#include "dooble_cryptography.h"

QAtomicInteger<quint64> dooble_cookies::s_db_id;

dooble_cookies::dooble_cookies(bool is_private, QObject *parent):QObject(parent)
{
  m_is_private = is_private;
}

void dooble_cookies::slot_cookie_added(const QNetworkCookie &cookie) const
{
  if(!dooble::s_cryptography || !dooble::s_cryptography->authenticated())
    return;
  else if(m_is_private)
    return;

  QString database_name(QString("dooble_cookies_%1").
			arg(s_db_id.fetchAndAddOrdered(1)));

  {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", database_name);

    db.setDatabaseName(dooble_settings::setting("home_path").toString() +
		       QDir::separator() +
		       "dooble_cookies.db");

    if(db.open())
      {
	QSqlQuery query(db);

	query.exec("CREATE TABLE IF NOT EXISTS dooble_cookies ("
		   "raw_form BLOB NOT NULL, "
		   "raw_form_digest TEXT NOT NULL PRIMARY KEY)");
	query.exec("PRAGMA synchronous = OFF");
	query.prepare
	  ("INSERT OR REPLACE INTO dooble_cookies "
	   "(raw_form, raw_form_digest) VALUES (?, ?)");

	QByteArray bytes;
	bool ok = true;

	bytes = dooble::s_cryptography->encrypt_then_mac(cookie.toRawForm());

	if(!bytes.isEmpty())
	  query.addBindValue(bytes.toBase64());
	else
	  ok = false;

	bytes = dooble::s_cryptography->hmac(cookie.toRawForm());

	if(!bytes.isEmpty())
	  query.addBindValue(bytes.toBase64());
	else
	  ok = false;

	if(ok)
	  query.exec();
      }

    db.close();
  }

  QSqlDatabase::removeDatabase(database_name);
}

void dooble_cookies::slot_cookie_removed(const QNetworkCookie &cookie) const
{
  if(!dooble::s_cryptography || !dooble::s_cryptography->authenticated())
    return;
  else if(m_is_private)
    return;

  QString database_name(QString("dooble_cookies_%1").
			arg(s_db_id.fetchAndAddOrdered(1)));

  {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", database_name);

    db.setDatabaseName(dooble_settings::setting("home_path").toString() +
		       QDir::separator() +
		       "dooble_cookies.db");

    if(db.open())
      {
	QSqlQuery query(db);

	query.exec("PRAGMA synchronous = OFF");
	query.prepare("DELETE FROM dooble_cookies WHERE raw_form_digest = ?");

	QByteArray bytes(dooble::s_cryptography->hmac(cookie.toRawForm()));

	query.addBindValue(bytes.toBase64());
	query.exec();
      }

    db.close();
  }

  QSqlDatabase::removeDatabase(database_name);
}

void dooble_cookies::slot_populate(void)
{
  if(!dooble::s_cryptography || !dooble::s_cryptography->authenticated())
    return;

  QString database_name(QString("dooble_cookies_%1").
			arg(s_db_id.fetchAndAddOrdered(1)));

  {
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", database_name);

    db.setDatabaseName(dooble_settings::setting("home_path").toString() +
		       QDir::separator() +
		       "dooble_cookies.db");

    if(db.open())
      {
	QSqlQuery query(db);

	query.setForwardOnly(true);

	if(query.exec("SELECT raw_form FROM dooble_cookies"))
	  while(query.next())
	    {
	      QByteArray bytes
		(QByteArray::fromBase64(query.value(0).toByteArray()));

	      bytes = dooble::s_cryptography->mac_then_decrypt(bytes);

	      if(bytes.isEmpty())
		continue;

	      QList<QNetworkCookie> cookie = QNetworkCookie::parseCookies
		(bytes);

	      if(cookie.isEmpty())
		continue;

	      QWebEngineProfile *profile = QWebEngineProfile::defaultProfile();

	      profile->cookieStore()->setCookie(cookie.at(0));
	    }
      }

    db.close();
  }

  QSqlDatabase::removeDatabase(database_name);
}