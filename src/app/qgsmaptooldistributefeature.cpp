/***************************************************************************
 qgsmaptooldistributefeature.cpp  -  map tool for copying and distributing features by mouse drag
 ---------------------
 begin                : November 2025
 copyright            : (C) 2025 by Jacky Volpes
 email                : jacky dot volpes at oslandia dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/


#include "qgisapp.h"
#include "qgsmapcanvas.h"
#include "qgsrubberband.h"
#include "qgssnappingutils.h"
#include "qgsvectorlayer.h"
#include "qgsmaptooldistributefeature.h"
#include "qgsadvanceddigitizingdockwidget.h"

class FeatureFilter : public QgsPointLocator::MatchFilter
{
  public:
    FeatureFilter() {}
    bool acceptMatch( const QgsPointLocator::Match &match ) override { return match.hasEdge(); }
};


QgsMapToolDistributeFeature::QgsMapToolDistributeFeature( QgsMapCanvas *canvas )
  : QgsMapToolAdvancedDigitizing( canvas, QgisApp::instance()->cadDockWidget() )
{
  mToolName = tr( "Copy and distribute feature" );
}

QgsMapToolDistributeFeature::~QgsMapToolDistributeFeature()
{
  deleteRubberbands();
}

void QgsMapToolDistributeFeature::cadCanvasMoveEvent( QgsMapMouseEvent *e )
{
  if ( mFeaturesRubberBand )
  {
    if ( QgsVectorLayer *vlayer = currentVectorLayer() )
    {
      // When MapCanvas crs == layer crs, fast rubberband translation
      if ( vlayer->crs() == canvas()->mapSettings().destinationCrs() )
      {
        const QgsPointXY pointCanvasCoords = e->mapPoint();
        const double offsetX = pointCanvasCoords.x() - mStartPointMapCoords.x();
        const double offsetY = pointCanvasCoords.y() - mStartPointMapCoords.y();
        mFeaturesRubberBand->setTranslationOffset( offsetX, offsetY );
      }

      // Else, recreate the rubber band from the translated geometries
      else
      {
        const QgsPointXY startPointLayerCoords = toLayerCoordinates( ( QgsMapLayer * ) vlayer, mStartPointMapCoords );
        const QgsPointXY stopPointLayerCoords = toLayerCoordinates( ( QgsMapLayer * ) vlayer, e->mapPoint() );

        const double dx = stopPointLayerCoords.x() - startPointLayerCoords.x();
        const double dy = stopPointLayerCoords.y() - startPointLayerCoords.y();

        QgsGeometry geom = mFeatureGeom;

        if ( geom.translate( dx, dy ) == Qgis::GeometryOperationResult::Success )
        {
          mFeaturesRubberBand->setToGeometry( geom, vlayer );
        }
        else
        {
          mFeaturesRubberBand->reset( vlayer->geometryType() );
        }
      }
    }
  }
}

void QgsMapToolDistributeFeature::cadCanvasReleaseEvent( QgsMapMouseEvent *e )
{
  if ( e->button() != Qt::LeftButton )
    return;

  QgsVectorLayer *vlayer = currentVectorLayer();
  if ( !vlayer || !vlayer->isEditable() )
  {
    deleteRubberbands();
    cadDockWidget()->clear();
    notifyNotEditableLayer();
    return;
  }

  if ( vlayer->selectedFeatureCount() != 0 )
    return;

  const QgsPointXY layerCoords = toLayerCoordinates( vlayer, e->mapPoint() );
  const double searchRadius = QgsTolerance::vertexSearchRadius( mCanvas->currentLayer(), mCanvas->mapSettings() );
  const QgsRectangle selectRect( layerCoords.x() - searchRadius, layerCoords.y() - searchRadius, layerCoords.x() + searchRadius, layerCoords.y() + searchRadius );

  if ( !mFeaturesRubberBand )
  {
    //find the closest feature
    QgsFeatureIterator fit = vlayer->getFeatures( QgsFeatureRequest().setFilterRect( selectRect ).setNoAttributes() );
    const QgsGeometry pointGeometry = QgsGeometry::fromPointXY( layerCoords );
    if ( pointGeometry.isNull() )
    {
      cadDockWidget()->clear();
      return;
    }

    double minDistance = std::numeric_limits<double>::max();

    QgsFeature cf;
    QgsFeature f;
    while ( fit.nextFeature( f ) )
    {
      if ( f.hasGeometry() )
      {
        const double currentDistance = pointGeometry.distance( f.geometry() );
        if ( currentDistance < minDistance )
        {
          minDistance = currentDistance;
          cf = f;
        }
      }
    }

    if ( minDistance == std::numeric_limits<double>::max() )
    {
      cadDockWidget()->clear();
      return;
    }

    mFeatureId = cf.id();

    mFeatureGeom = cf.geometry();
    createFeaturesRubberBandGeometry( vlayer->geometryType() );
    mStartPointMapCoords = e->mapPoint();
  }
  else if ( !mReferenceLineRubberBand )
  {
    FeatureFilter filter;
    QgsPointLocator::Match match;
    match = mCanvas->snappingUtils()->snapToMap( e->mapPoint(), &filter );
    createReferenceRubberband( match );
  }
}

void QgsMapToolDistributeFeature::activate()
{
  QgsMapToolAdvancedDigitizing::activate();

  // Save the original snapping configuration
  mOriginalSnappingConfig = mCanvas->snappingUtils()->config();

  // Enable Snapping & Snapping on Segment
  QgsSnappingConfig snappingConfig = mOriginalSnappingConfig;
  snappingConfig.setEnabled( true );
  Qgis::SnappingTypes flags = snappingConfig.typeFlag();
  flags |= Qgis::SnappingType::Segment;
  snappingConfig.setTypeFlag( flags );
  mCanvas->snappingUtils()->setConfig( snappingConfig );
}

void QgsMapToolDistributeFeature::deactivate()
{
  deleteRubberbands();

  // Restore the original snapping configuration
  mCanvas->snappingUtils()->setConfig( mOriginalSnappingConfig );
  QgsMapToolAdvancedDigitizing::deactivate();
}

void QgsMapToolDistributeFeature::deleteRubberbands()
{
  mFeaturesRubberBand.reset();
  mReferenceLineRubberBand.reset();
}

void QgsMapToolDistributeFeature::createFeaturesRubberBandGeometry( Qgis::GeometryType geometryType )
{
  mFeaturesRubberBand.reset( createRubberBand( geometryType ) );
  if ( mFeatureGeom.isNull() )
    return;

  mFeaturesRubberBand->addGeometry( mFeatureGeom );
  mFeaturesRubberBand->show();

  // for ( int i = 0; i < mFeatureNb; ++i )
  // {
  //
  // }
}

void QgsMapToolDistributeFeature::createReferenceRubberband( QgsPointLocator::Match match )
{
  if ( !match.isValid() || !match.layer() )
  {
    return;
  }

  QgsVectorLayer *refLayer = match.layer();
  QgsPointXY p1, p2;
  match.edgePoints( p1, p2 );
  mReferenceLineRubberBand.reset( createRubberBand() );

  // Compute intersection between the line that extends the limit segment and the
  // edges of the map canvas
  QgsPoint canvasTopLeft = QgsPoint( toLayerCoordinates( refLayer, QPoint( 0, 0 ) ) );
  QgsPoint canvasTopRight = QgsPoint( toLayerCoordinates( refLayer, QPoint( mCanvas->width(), 0 ) ) );
  QgsPoint canvasBottomLeft = QgsPoint( toLayerCoordinates( refLayer, QPoint( 0, mCanvas->height() ) ) );
  QgsPoint canvasBottomRight = QgsPoint( toLayerCoordinates( refLayer, QPoint( mCanvas->width(), mCanvas->height() ) ) );

  QList<QgsPointXY> points;
  points << p1 << p2;

  QgsPoint intersection;
  if ( QgsGeometryUtils::lineIntersection( QgsPoint( p1 ), QgsPoint( p2 ) - QgsPoint( p1 ), canvasTopLeft, canvasTopRight - canvasTopLeft, intersection ) )
  {
    points << QgsPointXY( intersection );
  }
  if ( QgsGeometryUtils::lineIntersection( QgsPoint( p1 ), QgsPoint( p2 ) - QgsPoint( p1 ), canvasTopRight, canvasBottomRight - canvasTopRight, intersection ) )
  {
    points << QgsPointXY( intersection );
  }
  if ( QgsGeometryUtils::lineIntersection( QgsPoint( p1 ), QgsPoint( p2 ) - QgsPoint( p1 ), canvasBottomRight, canvasBottomLeft - canvasBottomRight, intersection ) )
  {
    points << QgsPointXY( intersection );
  }
  if ( QgsGeometryUtils::lineIntersection( QgsPoint( p1 ), QgsPoint( p2 ) - QgsPoint( p1 ), canvasBottomLeft, canvasTopLeft - canvasBottomLeft, intersection ) )
  {
    points << QgsPointXY( intersection );
  }

  // Reorder the points by x/y coordinates
  std::sort( points.begin(), points.end(), []( const QgsPointXY & a, const QgsPointXY & b ) -> bool
  {
    if ( a.x() == b.x() )
      return a.y() < b.y();
    return a.x() < b.x();
  } );

  // Keep only the closest intersection points from the original points
  const int p1Idx = points.indexOf( p1 );
  const int p2Idx = points.indexOf( p2 );
  const int first = std::max( 0, std::min( p1Idx, p2Idx ) - 1 );
  const int last = std::min( static_cast<int>( points.size() ) - 1, std::max( p1Idx, p2Idx ) + 1 );
  const QgsPolylineXY polyline = points.mid( first, last - first + 1 ).toVector();

  // Densify the polyline to display a more accurate prediction when layer crs != canvas crs
  QgsGeometry geom = QgsGeometry::fromPolylineXY( polyline ).densifyByCount( 10 );

  mReferenceLineRubberBand->setToGeometry( geom, refLayer );
  mReferenceLineRubberBand->show();
}
