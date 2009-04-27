/***************************************************************************
                       documentman.cpp  - Document Manager
                             -------------------
    begin                : 2006
    copyright            : (C) 2006 by Klaas Freitag
    email                : freitag@kde.org
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include <qsqlquery.h>
#include <qsqlcursor.h>

#include <kstaticdeleter.h>
#include <kdebug.h>

#include "documentman.h"
#include "docdigest.h"
#include "kraftdb.h"

static KStaticDeleter<DocumentMan> selfDeleter;

DocumentMan *DocumentMan::mSelf = 0;
// DocGuardedPtr DocumentMan::mDocPtr = 0;
DocumentMap DocumentMan::mDocMap = DocumentMap();

DocumentMan *DocumentMan::self()
{
  if ( !mSelf ) {
    selfDeleter.setObject( mSelf, new DocumentMan() );
  }
  return mSelf;
}

DocumentMan::DocumentMan()
  : mColumnList( "docID, ident, docType, docDescription, clientID, lastModified, date, country, language, projectLabel" ),
    mFullTax( -1 ),
    mReducedTax( -1 )
{

}

DocDigestList DocumentMan::latestDocs( int limit )
{
  DocDigestList ret;

  QString qStr = QString( "SELECT %1 FROM document ORDER BY date desc" ).arg( mColumnList );

  if( limit > 0 )
    qStr += " LIMIT " + QString::number( limit );
  qStr +=";";
  kdDebug() << "Sending sql string " << qStr << endl;

  QSqlQuery query( qStr );

  if( query.isActive() ) {
    while( query.next() ) {
      ret.prepend( digestFromQuery( query ) );
    }
  }

  return ret;
}

DocDigest DocumentMan::digestFromQuery( QSqlQuery& query )
{
  DocDigest dig;
  QSqlCursor archCur( "archdoc" );

  dig.setId( dbID( query.value(0).toInt() ) );
  const QString ident = query.value(1).toString();
  dig.setIdent( ident );
  dig.setType(  query.value(2).toString() );
  dig.setWhiteboard( KraftDB::self()->mysqlEuroDecode( query.value( 3 ).toString() ) );
  dig.setClientId( query.value(4).toString() );
  dig.setLastModified( query.value(5).toDate() );
  dig.setDate(     query.value(6).toDate() );
  dig.setCountryLanguage(  query.value( 7 ).toString(), query.value( 8 ).toString() );
  dig.setProjectLabel( query.value( 9 ).toString() );
  // kdDebug() << "Adding document "<< ident << " to the latest list" << endl;

  archCur.select( "ident='" + ident +"'" );
  while ( archCur.next() ) {
    int id = archCur.value( "archDocID" ).toInt();
    QDateTime dt = archCur.value( "printDate" ).toDateTime();
    int state = archCur.value( "state" ).toInt();
    dig.addArchDocDigest( ArchDocDigest( dt, state, ident, id ) );
  }
  return dig;
}

DocDigestsTimelineList DocumentMan::docsTimelined()
{
  DocDigestsTimelineList retList; // a list of timelined digest objects

  QString qStr = QString( "SELECT %1, MONTH(date) as month, YEAR(date) as year FROM document ORDER BY date asc;" ).arg( mColumnList );

  QSqlQuery query( qStr );
  DocDigestsTimeline timeline;
  DocDigestList digests;

  if( query.isActive() ) {
    while( query.next() ) {
      DocDigest dig = digestFromQuery( query );
      int month = query.value( 9 /* month */ ).toInt();
      int year = query.value( 10 /* year */ ).toInt();
      // kdDebug() << "Month: " << month << " in Year: " << year << endl;

      if ( timeline.month() == 0 ) timeline.setMonth( month );
      if ( timeline.year() == 0  ) timeline.setYear( year );

      // kdDebug() << "timeline-month=" << timeline.month() << " while month=" << month << endl;
      if ( month != timeline.month() || year != timeline.year() ) {
        // a new month/year pair: set digestlist to timelineobject
        timeline.setDigestList( digests );

        retList.append( timeline );

        digests.clear();
        digests.prepend( dig );

        timeline.clearDigestList();
        timeline.setMonth( month );
        timeline.setYear( year );
      } else {
        digests.prepend( dig );
        // kdDebug() << "Prepending to digests lists: " << dig.date() << endl;
      }
    }
    kdDebug() << "Final append !" << endl;
    timeline.setDigestList( digests );
    retList.append( timeline );

  }
  return retList;
}

DocGuardedPtr DocumentMan::createDocument( const QString& copyFromId )
{
  DocGuardedPtr doc = new KraftDoc( );
  doc->newDocument();
  kdDebug() << "new document ID: " << doc->docID().toString() << endl;
  mDocMap[doc->docID().toString()] = doc;

  if ( ! copyFromId.isEmpty() ) {
    // copy the content from the source document to the new doc.
    DocGuardedPtr sourceDoc = openDocument( copyFromId );
    if ( sourceDoc ) {
      *doc = *sourceDoc;
    }
  }

  return doc;
}

DocGuardedPtr DocumentMan::openDocument( const QString& id )
{
  kdDebug() << "Opening Document with id " << id << endl;
  DocGuardedPtr doc;

  if( mDocMap.contains( id ) ){
    doc = mDocMap[id];
  } else {
    doc = new KraftDoc();
    doc->openDocument( id );
    mDocMap[id] = doc;
  }
  return doc;
}

QStringList DocumentMan::openDocumentsList()
{
  QStringList list;

  DocumentMap::Iterator it;
  for ( it = mDocMap.begin(); it != mDocMap.end(); ++it ) {
    DocGuardedPtr doc = it.data();
    list.append( doc->docIdentifier() );
  }
  return list;
}

void DocumentMan::clearTaxCache()
{
  mFullTax = -1;
  mReducedTax = -1;
}

double DocumentMan::tax( const QDate& date )
{
  if ( mFullTax < 0 || date != mTaxDate )
    readTaxes( date );
  return mFullTax;
}

double DocumentMan::reducedTax( const QDate& date )
{
  if ( mReducedTax < 0 || date != mTaxDate )
    readTaxes( date );
  return mReducedTax;
}

bool DocumentMan::readTaxes( const QDate& date )
{
  QString sql;
  QSqlQuery q;
  sql = "SELECT fullTax, reducedTax, startDate FROM taxes ";
  sql += "WHERE startDate <= :date ORDER BY startDate DESC LIMIT 1";

  q.prepare( sql );
  QString dateStr = date.toString( "yyyy-MM-dd" );
  kdDebug() << "** Datestring: " << dateStr << endl;
  q.bindValue( ":date", dateStr );
  q.exec();

  if ( q.next() ) {
    mFullTax    = q.value( 0 ).toDouble();
    mReducedTax = q.value( 1 ).toDouble();
    mTaxDate = date;
    kdDebug() << "* Taxes: " << mFullTax << "/" << mReducedTax << " from " << q.value( 2 ).toDate() << endl;
  }
  return ( mFullTax > 0 && mReducedTax > 0 );
}

DocumentMan::~DocumentMan()
{

}

