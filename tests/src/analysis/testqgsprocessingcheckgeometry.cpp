/***************************************************************************
                         testqgsprocessingcheckgeometry.cpp
                         ---------------------
    begin                : December 2023
    copyright            : (C) 2023 by Jacky Volpes
    email                : jacky dot volpes at oslandia dot com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "qgsnativealgorithms.h"
#include "qgsprocessingregistry.h"
#include "qgstest.h"
#include "qgsvectorlayer.h"
#include <algorithm>

class TestQgsProcessingCheckGeometry: public QgsTest
{
    Q_OBJECT

  public:
    TestQgsProcessingCheckGeometry() : QgsTest( QStringLiteral( "Processing Algorithms Check Geometry" ) ) {}

  private slots:
    void initTestCase();// will be called before the first testfunction is executed.
    void cleanupTestCase(); // will be called after the last testfunction was executed.
    void init() {} // will be called before each testfunction is executed.
    void cleanup() {} // will be called after every testfunction.

    void angleAlg_data();
    void angleAlg();

  private:
    QString mPointLayerPath = QDir( TEST_DATA_DIR ).absoluteFilePath( "geometry_checker/point_layer.shp" );
    QString mLineLayerPath = QDir( TEST_DATA_DIR ).absoluteFilePath( "geometry_checker/line_layer.shp" );
    QString mPolygonLayerPath = QDir( TEST_DATA_DIR ).absoluteFilePath( "geometry_checker/polygon_layer.shp" );
};

void TestQgsProcessingCheckGeometry::initTestCase()
{
  QgsApplication::init();
  QgsApplication::initQgis();

  // Set up the QgsSettings environment
  QCoreApplication::setOrganizationName( QStringLiteral( "QGIS" ) );
  QCoreApplication::setOrganizationDomain( QStringLiteral( "qgis.org" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "QGIS-TEST" ) );

  QgsApplication::processingRegistry()->addProvider( new QgsNativeAlgorithms( QgsApplication::processingRegistry() ) );
}

void TestQgsProcessingCheckGeometry::cleanupTestCase()
{
  QgsApplication::exitQgis();
}

void TestQgsProcessingCheckGeometry::angleAlg_data()
{
  QTest::addColumn<QStringList>( "layersToTest" );
  QTest::addColumn<QList<int>>( "expectedErrorsCount" );
  QTest::addColumn<int>( "expectedLoadedLayersCount" );
  QTest::newRow( "Line and Polygon layer" ) << ( QStringList() << mLineLayerPath << mPolygonLayerPath ) << ( QList<int>() << 4 << 4 ) << 2;
  QTest::newRow( "Point and Polygon layer" ) << ( QStringList() << mPointLayerPath << mPolygonLayerPath ) << ( QList<int>() << 4 ) << 1;
}

void TestQgsProcessingCheckGeometry::angleAlg()
{
  QFETCH( QStringList, layersToTest );
  QFETCH( QList<int>, expectedErrorsCount );
  QFETCH( int, expectedLoadedLayersCount );

  std::unique_ptr< QgsProcessingAlgorithm > alg(
    QgsApplication::processingRegistry()->createAlgorithmById( QStringLiteral( "native:checkgeometryangle" ) )
  );
  QVERIFY( alg != nullptr );

  QVariantMap parameters;
  parameters.insert( QStringLiteral( "INPUTS" ), QVariant::fromValue( layersToTest ) );
  parameters.insert( QStringLiteral( "MIN_ANGLE" ), 15 );
  parameters.insert( QStringLiteral( "ERRORS" ), QgsProcessing::TEMPORARY_OUTPUT );
  parameters.insert( QStringLiteral( "LOAD_OUTPUTS" ), true );

  bool ok = false;
  QgsProcessingFeedback feedback;
  std::unique_ptr< QgsProcessingContext > context = std::make_unique< QgsProcessingContext >();

  QVariantMap results;
  results = alg->run( parameters, *context, &feedback, &ok );
  QVERIFY( ok );

  QCOMPARE( context->layersToLoadOnCompletion().count(), expectedLoadedLayersCount );

  std::unique_ptr<QgsVectorLayer> errorsLayer( qobject_cast< QgsVectorLayer * >( context->getMapLayer( results.value( QStringLiteral( "ERRORS" ) ).toString() ) ) );
  QVERIFY( errorsLayer->isValid() );

  int totalExpectedErrorsCount = 0;
  for ( int expectedErrorCount : expectedErrorsCount )
    totalExpectedErrorsCount += expectedErrorCount;
  QCOMPARE( errorsLayer->featureCount(), totalExpectedErrorsCount );
}

QGSTEST_MAIN( TestQgsProcessingCheckGeometry )
#include "testqgsprocessingcheckgeometry.moc"
