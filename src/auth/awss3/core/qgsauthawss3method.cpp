/***************************************************************************
  qgsauthawss3method.cpp
  --------------------------------------
  Date                 : December 2022
  Copyright            : (C) 2022 by Jacky Volpes
  Email                : jacky dot volpes at oslandia dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsauthawss3method.h"

#include <QUrlQuery>
#include <QDateTime>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>

#include "qgsauthmanager.h"
#include "qgslogger.h"
#include "qgsapplication.h"

#ifdef HAVE_GUI
#include "qgsauthawss3edit.h"
#endif


const QString QgsAuthAwsS3Method::AUTH_METHOD_KEY = QStringLiteral( "AWSS3" );
const QString QgsAuthAwsS3Method::AUTH_METHOD_DESCRIPTION = QStringLiteral( "AWS S3" );
const QString QgsAuthAwsS3Method::AUTH_METHOD_DISPLAY_DESCRIPTION = tr( "AWS S3" );

QMap<QString, QgsAuthMethodConfig> QgsAuthAwsS3Method::sAuthConfigCache = QMap<QString, QgsAuthMethodConfig>();


QgsAuthAwsS3Method::QgsAuthAwsS3Method()
{
  setVersion( 4 );
  setExpansions( QgsAuthMethod::NetworkRequest | QgsAuthMethod::DataSourceUri );
  setDataProviders( QStringList()
                    << QStringLiteral( "awss3" )
                    << QStringLiteral( "ogr" )
                    << QStringLiteral( "gdal" ) );
}

QString QgsAuthAwsS3Method::key() const
{
  return AUTH_METHOD_KEY;
}

QString QgsAuthAwsS3Method::description() const
{
  return AUTH_METHOD_DESCRIPTION;
}

QString QgsAuthAwsS3Method::displayDescription() const
{
  return AUTH_METHOD_DISPLAY_DESCRIPTION;
}

bool QgsAuthAwsS3Method::updateNetworkRequest( QNetworkRequest &request, const QString &authcfg,
    const QString &dataprovider )
{
  Q_UNUSED( dataprovider )
  const QgsAuthMethodConfig config = getMethodConfig( authcfg );
  if ( !config.isValid() )
  {
    QgsDebugError( QStringLiteral( "Update request config FAILED for authcfg: %1: config invalid" ).arg( authcfg ) );
    return false;
  }

  const QByteArray username = config.config( QStringLiteral( "username" ) ).toLocal8Bit();
  const QByteArray password = config.config( QStringLiteral( "password" ) ).toLocal8Bit();
  const QByteArray region = config.config( QStringLiteral( "region" ) ).toLocal8Bit();

  const QByteArray headerList = "host;x-amz-content-sha256;x-amz-date";
  const QByteArray encryptionMethod = "AWS4-HMAC-SHA256";
  const QDateTime currentDateTime = QDateTime::currentDateTime().toUTC();
  const QByteArray date = currentDateTime.toString( "yyyyMMdd" ).toLocal8Bit();
  const QByteArray dateTime = currentDateTime.toString( "yyyyMMddThhmmssZ" ).toLocal8Bit();

  QByteArray canonicalPath = QUrl::toPercentEncoding( request.url().path(), "/" );  // Don't encode slash
  if ( canonicalPath.isEmpty() )
  {
    canonicalPath = "/";
  }

  QByteArray method;
  QByteArray payloadHash;
  if ( request.hasRawHeader( "X-Amz-Content-SHA256" ) )
  {
    method = "PUT";
    payloadHash = request.rawHeader( "X-Amz-Content-SHA256" );
  }
  else
  {
    method = "GET";
    payloadHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";  // Sha256 of empty payload
    request.setRawHeader( QByteArray( "X-Amz-Content-SHA256" ), payloadHash );
  }

  const QByteArray canonicalRequest = method + '\n' +
                                      canonicalPath + '\n' +
                                      '\n' +
                                      "host:" + request.url().host().toLocal8Bit() + '\n' +
                                      "x-amz-content-sha256:" + payloadHash + '\n' +
                                      "x-amz-date:" + dateTime + '\n' +
                                      '\n' +
                                      headerList + '\n' +
                                      payloadHash;

  const QByteArray canonicalRequestHash = QCryptographicHash::hash( canonicalRequest, QCryptographicHash::Sha256 ).toHex();
  const QByteArray stringToSign = encryptionMethod + '\n' +
                                  dateTime + '\n' +
                                  date + "/" + region + "/s3/aws4_request" + '\n' +
                                  canonicalRequestHash;

  const QByteArray signingKey = QMessageAuthenticationCode::hash( "aws4_request",
                                QMessageAuthenticationCode::hash( "s3",
                                    QMessageAuthenticationCode::hash( region,
                                        QMessageAuthenticationCode::hash( date, "AWS4" + password,
                                            QCryptographicHash::Sha256 ),
                                        QCryptographicHash::Sha256 ),
                                    QCryptographicHash::Sha256 ),
                                QCryptographicHash::Sha256 );

  const QByteArray signature = QMessageAuthenticationCode::hash( stringToSign, signingKey, QCryptographicHash::Sha256 ).toHex();

  request.setRawHeader( QByteArray( "Host" ), request.url().host().toLocal8Bit() );
  request.setRawHeader( QByteArray( "X-Amz-Date" ), dateTime );
  request.setRawHeader( QByteArray( "Authorization" ),
                        encryptionMethod + "Credential=" + username + '/' + date + "/" + region + "/s3/aws4_request, SignedHeaders=" + headerList + ", Signature=" + signature );

  return true;
}

bool QgsAuthAwsS3Method::updateDataSourceUriItems( QStringList &connectionItems, const QString &authcfg,
    const QString &dataprovider )
{
  Q_UNUSED( dataprovider )
  const QMutexLocker locker( &mMutex );
  const QgsAuthMethodConfig config = getMethodConfig( authcfg );
  if ( !config.isValid() )
  {
    QgsDebugError( QStringLiteral( "Update URI items FAILED for authcfg: %1: basic config invalid" ).arg( authcfg ) );
    return false;
  }

  if ( connectionItems.isEmpty() )
  {
    QgsDebugError( QStringLiteral( "Connection Items is empty!" ) );
    return false;
  }

  const QByteArray username = config.config( QStringLiteral( "username" ) ).toLocal8Bit();
  const QByteArray password = config.config( QStringLiteral( "password" ) ).toLocal8Bit();
  const QByteArray region = config.config( QStringLiteral( "region" ) ).toLocal8Bit();

  const QByteArray headerList = "host";
  const QByteArray encryptionMethod = "AWS4-HMAC-SHA256";
  const QDateTime currentDateTime = QDateTime::currentDateTime().toUTC();
  const QByteArray date = currentDateTime.toString( "yyyyMMdd" ).toLocal8Bit();
  const QByteArray dateTime = currentDateTime.toString( "yyyyMMddThhmmssZ" ).toLocal8Bit();
  const QByteArray expires = "300";
  const QByteArray method = "GET";

  const QString fullUri( connectionItems.first() );
  QString uri( fullUri );

  QByteArray canonicalPath = QUrl::toPercentEncoding( QUrl( uri.mid( 9 ) ).path(), "/" );
  if ( canonicalPath.isEmpty() )
  {
    canonicalPath = "/";
  }

  const QByteArray canonicalRequest = method + '\n' +
                                      canonicalPath + '\n' +
                                      "X-Amz-Algorithm=" + encryptionMethod + '&' +
                                      "X-Amz-Credential=" + username + "%2F" + date + "%2F" + region + "%2Fs3%2Faws4_request" + '&' +
                                      "X-Amz-Date=" + dateTime + '&' +
                                      "X-Amz-Expires=" + expires + '&' +
                                      "X-Amz-SignedHeaders=" + headerList + '\n' +
                                      "host:" + QUrl( uri.mid( 9 ) ).host().toLocal8Bit() + '\n' +
                                      '\n' +
                                      headerList + '\n' +
                                      "UNSIGNED-PAYLOAD";
  qDebug() << "canonicalRequest" << canonicalRequest << "###";

  const QByteArray canonicalRequestHash = QCryptographicHash::hash( canonicalRequest, QCryptographicHash::Sha256 ).toHex();
  const QByteArray stringToSign = encryptionMethod + '\n' +
                                  dateTime + '\n' +
                                  date + "/" + region + "/s3/aws4_request" + '\n' +
                                  canonicalRequestHash;

  const QByteArray signingKey = QMessageAuthenticationCode::hash( "aws4_request",
                                QMessageAuthenticationCode::hash( "s3",
                                    QMessageAuthenticationCode::hash( region,
                                        QMessageAuthenticationCode::hash( date, "AWS4" + password,
                                            QCryptographicHash::Sha256 ),
                                        QCryptographicHash::Sha256 ),
                                    QCryptographicHash::Sha256 ),
                                QCryptographicHash::Sha256 );

  const QByteArray signature = QMessageAuthenticationCode::hash( stringToSign, signingKey, QCryptographicHash::Sha256 ).toHex();

  uri += QStringLiteral( "?X-Amz-Algorithm=%1" ).arg( QString::fromLocal8Bit( encryptionMethod ) );
  uri += QStringLiteral( "&X-Amz-Credential=%1" ).arg( QString::fromLocal8Bit( username + "%2F" + date + "%2F" + region + "%2Fs3%2Faws4_request" ) );
  uri += QStringLiteral( "&X-Amz-Date=%1" ).arg( QString::fromLocal8Bit( dateTime ) );
  uri += QStringLiteral( "&X-Amz-SignedHeaders=%1" ).arg( QString::fromLocal8Bit( headerList ) );
  uri += QStringLiteral( "&X-Amz-Signature=%1" ).arg( QString::fromLocal8Bit( signature ) );
  uri += QStringLiteral( "&X-Amz-Expires=%1" ).arg( QString::fromLocal8Bit( expires ) );

  connectionItems.replace( 0, uri );
  return true;
}

void QgsAuthAwsS3Method::clearCachedConfig( const QString &authcfg )
{
  removeMethodConfig( authcfg );
}

void QgsAuthAwsS3Method::updateMethodConfig( QgsAuthMethodConfig &mconfig )
{
  Q_UNUSED( mconfig );
  // NOTE: add updates as method version() increases due to config storage changes
}

QgsAuthMethodConfig QgsAuthAwsS3Method::getMethodConfig( const QString &authcfg, bool fullconfig )
{
  const QMutexLocker locker( &mMutex );
  QgsAuthMethodConfig config;

  // check if it is cached
  if ( sAuthConfigCache.contains( authcfg ) )
  {
    config = sAuthConfigCache.value( authcfg );
    QgsDebugMsgLevel( QStringLiteral( "Retrieved config for authcfg: %1" ).arg( authcfg ), 2 );
    return config;
  }

  // else build bundle
  if ( !QgsApplication::authManager()->loadAuthenticationConfig( authcfg, config, fullconfig ) )
  {
    QgsDebugMsgLevel( QStringLiteral( "Retrieved config for authcfg: %1" ).arg( authcfg ), 2 );
    return QgsAuthMethodConfig();
  }

  // cache bundle
  putMethodConfig( authcfg, config );

  return config;
}

void QgsAuthAwsS3Method::putMethodConfig( const QString &authcfg, const QgsAuthMethodConfig &mconfig )
{
  const QMutexLocker locker( &mMutex );
  QgsDebugMsgLevel( QStringLiteral( "Putting AWS S3 config for authcfg: %1" ).arg( authcfg ), 2 );
  sAuthConfigCache.insert( authcfg, mconfig );
}

void QgsAuthAwsS3Method::removeMethodConfig( const QString &authcfg )
{
  const QMutexLocker locker( &mMutex );
  if ( sAuthConfigCache.contains( authcfg ) )
  {
    sAuthConfigCache.remove( authcfg );
    QgsDebugMsgLevel( QStringLiteral( "Removed Aws S3 config for authcfg: %1" ).arg( authcfg ), 2 );
  }
}

#ifdef HAVE_GUI
QWidget *QgsAuthAwsS3Method::editWidget( QWidget *parent ) const
{
  return new QgsAuthAwsS3Edit( parent );
}
#endif

//////////////////////////////////////////////
// Plugin externals
//////////////////////////////////////////////


#ifndef HAVE_STATIC_PROVIDERS
QGISEXTERN QgsAuthMethodMetadata *authMethodMetadataFactory()
{
  return new QgsAuthAwsS3MethodMetadata();
}
#endif
