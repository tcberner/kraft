/***************************************************************************
             templatesaverdb  -
                             -------------------
    begin                : 2005-20-00
    copyright            : (C) 2005 by Klaas Freitag
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

// include files for Qt
#include <QSqlRecord>
#include <QSqlQuery>
#include <QSqlTableModel>

// include files for KDE
#include <klocale.h>
#include <kdebug.h>

#include "kraftdb.h"
#include "kraftglobals.h"
#include "dbids.h"
#include "templatesaverdb.h"
#include "calcpart.h"
#include "floskeltemplate.h"
#include "zeitcalcpart.h"
#include "fixcalcpart.h"
#include "materialcalcpart.h"
#include "stockmaterial.h"


bool CalculationsSaverDB::saveFixCalcPart( FixCalcPart *cp, dbID parentID )
{
    bool result = true;
    QSqlTableModel model;
    model.setTable(mTableFixCalc);
    int cpId = cp->getDbID().toInt();
    model.setFilter("FCalcID=" + QString::number( cpId ));
    model.select();
    kDebug() << "CalcFix calcpart-ID is " << cpId << endl;
    if( cpId < 0 ) { // no db entry yet => INSERT
        if( !cp->isToDelete() ) {
            QSqlRecord buffer = model.record();
            fillFixCalcBuffer( &buffer, cp );
            buffer.setValue( "TemplID", parentID.toInt() );
            model.insertRecord(-1, buffer);
            model.submitAll();

            dbID id = KraftDB::self()->getLastInsertID();
            kDebug() << "Setting db-ID " << id.toString() << endl;
            cp->setDbID(id);
        } else {
            kDebug() << "new element, but set to delete" << endl;
        }
    } else {
        if( cp->isToDelete() ) {
            kDebug() << "deleting fix calc part " << cpId << endl;
            // delete this calcpart.
            if ( model.rowCount() > 0 ) {
                int cnt = model.rowCount();
                model.removeRows(0, cnt);
                model.submitAll();
                kDebug() << "Amount of deleted entries: " << cnt << endl;
            }
        } else {
            // der Datensatz ist bereits in der Datenbank => UPDATE
            if( model.rowCount() > 0 ) {
                QSqlRecord buffer = model.record(0);
                buffer.setValue( "modDate", "systimestamp" );
                fillFixCalcBuffer(& buffer, cp );
                model.setRecord(0, buffer);
                model.submitAll();
            } else {
                kError() << "Can not select FCalcID, corrupt data!" << endl;
            }
        }
    }

    return result;
}

void CalculationsSaverDB::fillFixCalcBuffer( QSqlRecord *buffer, FixCalcPart *cp )
{
    if( ! (buffer && cp )) return;
    buffer->setValue( "name", cp->getName() );
    buffer->setValue( "amount", cp->getMenge() );

    buffer->setValue( "price", cp->unitPreis().toDouble() );

    buffer->setValue( "percent", cp->getProzentPlus() );
    buffer->setValue( "modDate", "systimestamp" );
}

bool CalculationsSaverDB::saveMaterialCalcPart( MaterialCalcPart *cp, dbID parentID )
{
  bool result = true;
  if( !cp ) return result;

  QSqlTableModel model;
  model.setTable( mTableMatCalc );

  int cpId = cp->getDbID().toInt();
  model.setFilter("MCalcID=" + QString::number( cpId ));
  model.select();
  kDebug() << "Saving material calcpart id=" << cpId << endl;

  if( cpId < 0 ) { // kein Eintrag in db bis jetzt => INSERT
    QSqlRecord buffer = model.record();
    fillMatCalcBuffer( &buffer, cp );
    buffer.setValue( "TemplID", parentID.toInt() );
    model.insertRecord(-1, buffer);
    model.submitAll();

    dbID id = KraftDB::self()->getLastInsertID();
    cp->setDbID(id);
  } else {
    // calcpart-ID ist bereits belegt, UPDATE
    if( model.rowCount() > 0) {
      QSqlRecord buffer = model.record(0);
      buffer.setValue( "modDate", "systimestamp" );
      fillMatCalcBuffer( &buffer, cp );
      model.setRecord(0, buffer);
      model.submitAll();
    } else {
      kError() << "Can not select MCalcID, corrupt data!" << endl;
    }
  }

  // nun die Materialliste sichern
  StockMaterialList matList = cp->getCalcMaterialList();
  StockMaterialListIterator it( matList );

  while( it.hasNext() ) {
    StockMaterial *mat = it.next();

    storeMaterialDetail( cp, mat );
  }

return result;
}

void CalculationsSaverDB::storeMaterialDetail( MaterialCalcPart *cp, StockMaterial *mat )
{
    if( ! (cp && mat) ) return;
    kDebug() << "storing material calcpart detail for material " << mat->name() << endl;

    /* create temporar dbcalcpart and fill the current material list */
    QSqlQuery q;
    q.prepare("SELECT amount, materialID FROM " + mTableMatDetailCalc + " WHERE CalcID=:CalcID AND materialID=:materialID");
    q.bindValue(":CalcID", cp->getDbID().toInt());
    q.bindValue(":materialID", mat->getID());
    q.exec();

    QString selStr = QString("CalcID=%1 AND materialID=%2" ).arg(cp->getDbID().toInt()).arg(mat->getID());

    kDebug() << "Material details for calcID " << cp->getDbID().toString() << endl;

    MaterialCalcPart dbPart("MatCalcPartonDB", 0 );
    while( q.next() )
    {
      double amount = q.value(0).toDouble();
      int matID     = q.value(1).toInt();
      dbPart.addMaterial(amount, matID);
    }

    /* Now start to compare the DB and the temp calc part */
    double newAmount = cp->getCalcAmount(mat);
    double origAmount = dbPart.getCalcAmount(mat);

    kDebug() << "The new Value is " << newAmount << " and the orig is " << origAmount << endl;
    QSqlTableModel model;
    model.setTable(mTableMatDetailCalc);
    model.setFilter(selStr);
    model.select();

    if( origAmount > -1.0  ) {
        // Es gibt schon einen DS fuer dieses Material, schauen, ob die Anzahl
        // des Materials stimmt, wenn nicht, updaten.
        if( origAmount != newAmount )
        {
            if( model.rowCount() > 0 )
            {
                QSqlRecord upRec = model.record(0);
                upRec.setValue("amount", newAmount);
                model.setRecord(0, upRec);
                model.submitAll();
            }
        }
            // muss geupdatet werder
        else {
            // die Anzahl ist gleichgeblieben, nix zu tun.
        }
    } else {
        // nix gefunden, datensatz muss eingefuegt werden.
        QSqlRecord insRec = model.record();
        insRec.setValue("amount", newAmount);
        insRec.setValue("CalcID", cp->getDbID().toInt());
        insRec.setValue("materialID", mat->getID());

        model.insertRecord(-1, insRec);
        model.submitAll();
        dbID id = KraftDB::self()->getLastInsertID();
        if( id.isOk() ) {
            cp->setDbID(id);
        } else {
            kDebug() << "ERROR: Keine gueltige DB-ID bei Anlage des Material CalcPart!" << endl;
        }
    }
}


void CalculationsSaverDB::fillMatCalcBuffer( QSqlRecord *buffer, MaterialCalcPart *cp )
{
    if( !(buffer && cp)) return;

    buffer->setValue("name", cp->getName());
    buffer->setValue("percent", cp->getProzentPlus() );

}

CalculationsSaverDB::CalculationsSaverDB( )
  : CalculationsSaverBase(),
    mTableTimeCalc( "CalcTime" ),
    mTableFixCalc( "CalcFixed" ),
    mTableMatCalc( "CalcMaterials" ),
    mTableMatDetailCalc( "CalcMaterialDetails" )
{

}

CalculationsSaverDB::CalculationsSaverDB( TargetType tt )
  : CalculationsSaverBase( tt ),
    mTableTimeCalc( "CalcTime" ),
    mTableFixCalc( "CalcFixed" ),
    mTableMatCalc( "CalcMaterials" ),
    mTableMatDetailCalc( "CalcMaterialDetails" )
{
  if ( tt == Document ) {
    mTableTimeCalc = "DocCalcTime";
    mTableFixCalc = "DocCalcFixed";
    mTableMatCalc = "DocCalcMaterials";
    mTableMatDetailCalc = "DocCalcMaterialDetails";
  }
}

bool CalculationsSaverDB::saveCalculations( CalcPartList parts, dbID parentID )
{
  bool res = true;

  CalcPartListIterator it( parts );

  while( it.hasNext()) {
    CalcPart *cp = it.next();
    if( cp->isDirty() )
    {
      if( cp->getType() == KALKPART_TIME ) {
        res = saveTimeCalcPart( static_cast<ZeitCalcPart*>(cp), parentID );
        Q_ASSERT( res );
      } else if( cp->getType() == KALKPART_FIX ) {
        res = saveFixCalcPart( static_cast<FixCalcPart*>(cp), parentID );
        Q_ASSERT( res );
      } else if( cp->getType() == KALKPART_MATERIAL ) {
        res = saveMaterialCalcPart( static_cast<MaterialCalcPart*>(cp), parentID );
        Q_ASSERT( res );
      } else {
        kDebug() << "ERROR: Unbekannter Kalkulations-Anteil-Typ!" << endl;
      }
    }
  }

  return res;
}

bool CalculationsSaverDB::saveTimeCalcPart( ZeitCalcPart *cp, dbID parentId )
{
    bool result = true;
    if( !cp ) return result;

    int cpId = cp->getDbID().toInt();

    QSqlTableModel model;
    model.setTable( mTableTimeCalc );
    model.setFilter( "TCalcID="+QString::number(cpId) );
    model.select();

    kDebug() << "Models last error: " << model.lastError() << model.rowCount();

    if( cpId < 0 )
    { // kein Eintrag in db bis jetzt => INSERT
        if( ! cp->isToDelete() ) {
            QSqlRecord buffer = model.record();
            fillZeitCalcBuffer( &buffer, cp );
            buffer.setValue( "TemplID", parentId.toInt() );
            model.insertRecord(-1, buffer);

            dbID id = KraftDB::self()->getLastInsertID();
            cp->setDbID(id);
        } else {
            kDebug() << "delete flag is set -> skip saving." << endl;
        }
    }

    else
    {
        if( cp->isToDelete() ) {
            // delete this calcpart.
            if ( model.rowCount() > 0 ) {
                model.removeRow(0);
                model.submitAll();
            }
        }
        else {
	    // Update needed, record is already in the database
            if( model.rowCount() > 0 ) {
                QSqlRecord buffer = model.record(0);
                buffer.setValue( "modDate", "systimestamp" );
                fillZeitCalcBuffer( &buffer, cp );
                model.setRecord(0, buffer);
                model.submitAll();
            } else {
                kError() << "Unable to select TCalcID, corrupt data!" << endl;
            }
        }
    }

    return result;
}

void CalculationsSaverDB::fillZeitCalcBuffer( QSqlRecord *buffer, ZeitCalcPart *cp )
{
    if( ! (buffer && cp )) return;
    buffer->setValue( "name",    cp->getName() );
    buffer->setValue( "minutes", cp->getMinuten() );
    buffer->setValue( "percent", cp->getProzentPlus() );

    StdSatz std = cp->getStundensatz();
    buffer->setValue( "stdHourSet", std.getId().toInt() );

    buffer->setValue( "allowGlobal", cp->globalStdSetAllowed() ? 1 : 0 );
}


/* =========================================================================== */

TemplateSaverDB::TemplateSaverDB( )
    : TemplateSaverBase()
{

}


TemplateSaverDB::~TemplateSaverDB( )
{

}

bool TemplateSaverDB::saveTemplate( FloskelTemplate *tmpl )
{
    bool res = true;
    bool isNew = false;

    // Transaktion ?

    QSqlTableModel model;
    model.setTable("Catalog");
    QString templID = QString::number(tmpl->getTemplID());
    model.setFilter("TemplID=" + templID);
    model.select();

    QSqlRecord buffer;
    if( model.rowCount() > 0)
    {
        kDebug() << "Updating template " << tmpl->getTemplID() << endl;

        // mach update
        isNew = false;
        buffer = model.record(0);
        fillTemplateBuffer( &buffer, tmpl, false );
        buffer.setValue( "modifyDatum", "systimestamp" );
        model.setRecord(0, buffer);
        model.submitAll();
    }
    else
    {
        // insert
        kDebug() << "Creating new database entry" << endl;

        isNew = true;
        buffer = model.record();
        fillTemplateBuffer( &buffer, tmpl, true );
        model.insertRecord(-1, buffer);
        model.submitAll();

        /* Jetzt die neue Template-ID selecten */
        dbID id = KraftDB::self()->getLastInsertID();
        kDebug() << "New Database ID=" << id.toInt() << endl;

        if( id.isOk() ) {
            tmpl->setTemplID(id.toInt() );
            templID = id.toString();
        } else {
            kDebug() << "ERROR: Kann AUTOINC nicht ermitteln" << endl;
            res = false;
        }
    }

    if( res )
    {
        /* Nun die einzelnen Calcparts speichern */
        CalcPartList parts = tmpl->getCalcPartsList();
        CalculationsSaverDB calculationSaver;
        res = calculationSaver.saveCalculations( parts, tmpl->getTemplID() );
    }
    return res;

}

void TemplateSaverDB::fillTemplateBuffer( QSqlRecord *buffer, FloskelTemplate *tmpl, bool isNew )
{
    buffer->setValue( "chapterID", tmpl->getChapterID());
    buffer->setValue( "unitID", tmpl->einheit().id());
    buffer->setValue( "Floskel", tmpl->getText().toUtf8() );
    buffer->setValue( "Gewinn", tmpl->getBenefit() );
    buffer->setValue( "zeitbeitrag", tmpl->hasTimeslice() );

    /* neue templates kriegen ein Eintragsdatum */
    QDateTime dt = QDateTime::currentDateTime();
    QString dtString = dt.toString("yyyy-MM-dd hh:mm:ss" );

    if( isNew ) {
        buffer->setValue( "enterDatum", dtString);
    }
    buffer->setValue("modifyDatum", dtString );

    int ctype = 2;  // Calculation type Calculation
    if( tmpl->calcKind() == CatalogTemplate::ManualPrice )
    {
        ctype = 1;
    }
    buffer->setValue( "Preisart", ctype );
    buffer->setValue( "EPreis", tmpl->manualPrice().toDouble() );
}

/* END */

