/***************************************************************************
 qgsmaptooldistributefeature.h  -  map tool for copying and distributing features by mouse drag
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

#ifndef QGSMAPTOOLDISTRIBUTEFEATURE_H
#define QGSMAPTOOLDISTRIBUTEFEATURE_H

#include "qgsfeatureid.h"
#include "qgsgeometry.h"
#include "qgsmaptooladvanceddigitizing.h"
#include "qgis_app.h"
#include "qgspointxy.h"
#include "qgssnappingconfig.h"
#include "qgspointlocator.h"


//! Map tool for copying and distributing features by mouse drag
class APP_EXPORT QgsMapToolDistributeFeature : public QgsMapToolAdvancedDigitizing
{
    Q_OBJECT
  public:
    QgsMapToolDistributeFeature( QgsMapCanvas *canvas );
    ~QgsMapToolDistributeFeature() override;

    void cadCanvasMoveEvent( QgsMapMouseEvent *e ) override;

    void cadCanvasReleaseEvent( QgsMapMouseEvent *e ) override;

    void deactivate() override;

    void activate() override;

  private:
    //! Start point of the move in map coordinates
    QgsPointXY mStartPointMapCoords;

    //! The current feature ID
    QgsFeatureId mFeatureId;

    //! The feature geometry
    QgsGeometry mFeatureGeom;

    //! The reference line geometry
    QgsGeometry mReferenceLineGeom;

    //! The number of features to distribute
    int mFeatureNb = 4;

    //! Rubberband that shows the feature being moved
    std::unique_ptr<QgsRubberBand> mFeaturesRubberBand;

    //! Rubberband that shows the reference line alog the features will be distributed
    std::unique_ptr<QgsRubberBand> mReferenceLineRubberBand;

    //! Snapping config that will be restored on deactivation
    QgsSnappingConfig mOriginalSnappingConfig;

    void deleteRubberbands();

    void createFeaturesRubberBandGeometry( Qgis::GeometryType geometryType );

    void createReferenceRubberband( QgsPointLocator::Match match );
};

#endif
