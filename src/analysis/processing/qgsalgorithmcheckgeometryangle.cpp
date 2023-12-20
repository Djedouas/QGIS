/***************************************************************************
                        qgsalgorithmcheckgeometryangle.cpp
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

#include "qgsalgorithmcheckgeometryangle.h"
#include "qgsgeometrycheckcontext.h"
#include "qgsgeometrycheckerror.h"
#include "qgsgeometryanglecheck.h"
#include "qgsmaplayer.h"
#include "qgspoint.h"
#include "qgsvectorlayer.h"
#include "qgsvectordataproviderfeaturepool.h"

///@cond PRIVATE

auto QgsGeometryCheckAngleAlgorithm::name() const -> QString
{
  return QStringLiteral( "checkgeometryangle" );
}

auto QgsGeometryCheckAngleAlgorithm::displayName() const -> QString
{
  return QObject::tr( "Check Geometry (Angle)" );
}

auto QgsGeometryCheckAngleAlgorithm::tags() const -> QStringList
{
  return QObject::tr( "check,geometry,angle" ).split( ',' );
}

auto QgsGeometryCheckAngleAlgorithm::group() const -> QString
{
  return QObject::tr( "Check geometry" );
}

auto QgsGeometryCheckAngleAlgorithm::groupId() const -> QString
{
  return QStringLiteral( "checkgeometry" );
}

auto QgsGeometryCheckAngleAlgorithm::shortHelpString() const -> QString
{
  return QObject::tr( "This algorithm check the angle of geometry (in line or polygon)." );
}

auto QgsGeometryCheckAngleAlgorithm::flags() const -> Qgis::ProcessingAlgorithmFlags
{
  return QgsProcessingAlgorithm::flags() | Qgis::ProcessingAlgorithmFlag::NoThreading;
}

auto QgsGeometryCheckAngleAlgorithm::createInstance() const -> QgsGeometryCheckAngleAlgorithm *
{
  return new QgsGeometryCheckAngleAlgorithm();
}

void QgsGeometryCheckAngleAlgorithm::initAlgorithm( const QVariantMap &configuration )
{
  Q_UNUSED( configuration )

  addParameter( new QgsProcessingParameterMultipleLayers( QStringLiteral( "INPUTS" ), QObject::tr( "Input layers" ), Qgis::ProcessingSourceType::VectorAnyGeometry ) );
  addParameter( new QgsProcessingParameterNumber( QStringLiteral( "MIN_ANGLE" ), QObject::tr( "min angle" ), Qgis::ProcessingNumberParameterType::Double, 0, false, 0.0, 180.0 ) );
  addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "ERRORS" ), QObject::tr( "Errors layer" ), Qgis::ProcessingSourceType::VectorPoint ) );
  addParameter( new QgsProcessingParameterBoolean( QStringLiteral( "LOAD_OUTPUTS" ), QObject::tr( "Load output layers upon completion" ), true ) );

  std::unique_ptr< QgsProcessingParameterNumber > tolerance = std::make_unique< QgsProcessingParameterNumber >( QStringLiteral( "TOLERANCE" ),
      QObject::tr( "Tolerance" ), Qgis::ProcessingNumberParameterType::Integer, 8, false, 1, 13 );
  tolerance->setFlags( tolerance->flags() | Qgis::ProcessingParameterFlag::Advanced );
  addParameter( tolerance.release() );
}

auto QgsGeometryCheckAngleAlgorithm::prepareAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback * ) -> bool
{
  mTolerance = parameterAsInt( parameters, QStringLiteral( "TOLERANCE" ), context );

  return true;
}

auto QgsGeometryCheckAngleAlgorithm::createFeaturePool( QgsVectorLayer *layer, bool selectedOnly ) const -> QgsFeaturePool *
{
  return new QgsVectorDataProviderFeaturePool( layer, selectedOnly );
}

static auto outputFields( ) -> QgsFields
{
  QgsFields fields;
  fields.append( QgsField( QStringLiteral( "gc_layerid" ), QMetaType::QString ) );
  fields.append( QgsField( QStringLiteral( "gc_layername" ), QMetaType::QString ) );
  fields.append( QgsField( QStringLiteral( "gc_featid" ), QMetaType::Int ) );
  fields.append( QgsField( QStringLiteral( "gc_partidx" ), QMetaType::Int ) );
  fields.append( QgsField( QStringLiteral( "gc_ringidx" ), QMetaType::Int ) );
  fields.append( QgsField( QStringLiteral( "gc_vertidx" ), QMetaType::Int ) );
  fields.append( QgsField( QStringLiteral( "gc_errorx" ), QMetaType::Double ) );
  fields.append( QgsField( QStringLiteral( "gc_errory" ), QMetaType::Double ) );
  fields.append( QgsField( QStringLiteral( "gc_error" ), QMetaType::QString ) );
  return fields;
}


auto QgsGeometryCheckAngleAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) -> QVariantMap
{
  QList<QgsMapLayer *> mapLayers = parameterAsLayerList( parameters, QStringLiteral( "INPUTS" ), context );
  bool loadOutputLayers = parameterAsBoolean( parameters, QStringLiteral( "LOAD_OUTPUTS" ), context );

  // Filter input layers with correct geometry type
  QMap<QString, QgsVectorLayer *> inputLayers;
  for ( QgsMapLayer *mapLayer : mapLayers )
  {
    if ( QgsVectorLayer *vectorLayer = dynamic_cast<QgsVectorLayer *>( mapLayer ) )
    {
      Qgis::GeometryType geomType = vectorLayer->geometryType();
      if ( geomType == Qgis::GeometryType::Line || geomType == Qgis::GeometryType::Polygon )
        inputLayers.insert( vectorLayer->id(), vectorLayer );
      else
        feedback->pushWarning( QObject::tr( "Layer %1 will not be processed: incorrect geometry type (point and line only)." ).arg( vectorLayer->name() ) );
    }
    else
      feedback->pushWarning( QObject::tr( "Layer %1 will not be processed: incorrect layer type (vector layers only)." ).arg( vectorLayer->name() ) );
  }
  if ( inputLayers.count() == 0 )
    throw QgsProcessingException( QObject::tr( "No eligible layer to process." ) );

  // Try to find a layer with a different CRS than the first layer
  auto refCrs = inputLayers.first()->crs();
  auto it = std::find_if( std::next( inputLayers.constBegin() ), inputLayers.constEnd(), [&]( QgsVectorLayer * v ) {return v->crs() != refCrs;} );
  if ( it != std::end( inputLayers ) )
    throw QgsProcessingException( QObject::tr( "CRS must be the same for all input layers." ) );

  QgsFields fields = outputFields();
  QString dest_errors;
  std::unique_ptr< QgsFeatureSink > sink_errors( parameterAsSink( parameters, QStringLiteral( "ERRORS" ), context, dest_errors, fields, Qgis::WkbType::Point, refCrs ) );
  if ( !sink_errors )
    throw QgsProcessingException( invalidSinkError( parameters, QStringLiteral( "ERRORS" ) ) );

  QgsProcessingMultiStepFeedback multiStepFeedback( 3, feedback );

  QgsProject *project = inputLayers.first()->project() ? inputLayers.first()->project() : QgsProject::instance();

  std::unique_ptr<QgsGeometryCheckContext> checkContext = std::make_unique<QgsGeometryCheckContext>( mTolerance, refCrs, project->transformContext(), project );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  double minAngle = parameterAsDouble( parameters, QStringLiteral( "MIN_ANGLE" ), context );

  QVariantMap configurationCheck;
  configurationCheck.insert( "minAngle", minAngle );
  const QgsGeometryAngleCheck check( checkContext.get(), configurationCheck );

  multiStepFeedback.setCurrentStep( 1 );
  feedback->setProgressText( QObject::tr( "Preparing features…" ) );
  QMap<QString, QgsFeaturePool *> featurePools;
  for ( QgsVectorLayer *inputLayer : inputLayers.values() )
    featurePools.insert( inputLayer->id(), createFeaturePool( inputLayer ) );

  multiStepFeedback.setCurrentStep( 2 );
  feedback->setProgressText( QObject::tr( "Collecting errors…" ) );
  check.collectErrors( featurePools, checkErrors, messages, feedback );

  multiStepFeedback.setCurrentStep( 3 );
  feedback->setProgressText( QObject::tr( "Exporting errors…" ) );
  double step{checkErrors.size() > 0 ? 100.0 / checkErrors.size() : 1};
  long i = 0;
  feedback->setProgress( 0.0 );

  // For storage of output layers
  QMap<QString, std::shared_ptr<QgsFeatureSink>> sinks;

  for ( QgsGeometryCheckError *error : checkErrors )
  {

    if ( feedback->isCanceled() )
      break;

    QgsFeature f;
    QgsAttributes attrs = f.attributes();
    QString layerId = error->layerId();
    QgsVectorLayer *inputLayer = inputLayers.value( layerId );
    QString layerName = inputLayer->name();

    attrs << layerId
          << layerName
          << error->featureId()
          << error->vidx().part
          << error->vidx().ring
          << error->vidx().vertex
          << error->location().x()
          << error->location().y()
          << error->value().toString();
    f.setAttributes( attrs );

    // Create sink if not exists yet
    if ( !sinks.contains( layerId ) )
    {
      QString dest_output;
      std::shared_ptr<QgsFeatureSink> sink( QgsProcessingUtils::createFeatureSink( dest_output, context, fields, inputLayer->wkbType(), refCrs ) );
      if ( loadOutputLayers )
        context.addLayerToLoadOnCompletion( dest_output, QgsProcessingContext::LayerDetails( layerName + "_output", project ) );
      sinks.insert( layerId, sink );
    }

    // Add feature in corresponding output layer
    f.setGeometry( error->geometry() );
    if ( !sinks.value( layerId )->addFeature( f, QgsFeatureSink::FastInsert ) )
      throw QgsProcessingException( writeFeatureError( sinks.value( layerId ).get(), parameters, QString( "%1 output" ).arg( layerName ) ) );

    // Add point in errors layer
    f.setGeometry( QgsGeometry::fromPoint( QgsPoint( error->location().x(), error->location().y() ) ) );
    if ( !sink_errors->addFeature( f, QgsFeatureSink::FastInsert ) )
      throw QgsProcessingException( writeFeatureError( sink_errors.get(), parameters, QStringLiteral( "ERRORS" ) ) );

    i++;
    feedback->setProgress( 100.0 * step * static_cast<double>( i ) );
  }

  QVariantMap outputs;
  outputs.insert( QStringLiteral( "ERRORS" ), dest_errors );

  return outputs;
}

///@endcond
