/***************************************************************************
                        qgsalgorithmcheckgeometrygap.cpp
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

#include "qgsalgorithmcheckgeometrygap.h"
#include "qgsgeometrycheckcontext.h"
#include "qgsgeometrycheckerror.h"
#include "qgsgeometrygapcheck.h"
#include "qgspoint.h"
#include "qgsvectorlayer.h"
#include "qgsvectordataproviderfeaturepool.h"

///@cond PRIVATE

auto QgsGeometryCheckGapAlgorithm::name() const -> QString
{
  return QStringLiteral( "checkgeometrygap" );
}

auto QgsGeometryCheckGapAlgorithm::displayName() const -> QString
{
  return QObject::tr( "Check Geometry (Gap)" );
}

auto QgsGeometryCheckGapAlgorithm::tags() const -> QStringList
{
  return QObject::tr( "check,geometry,gap" ).split( ',' );
}

auto QgsGeometryCheckGapAlgorithm::group() const -> QString
{
  return QObject::tr( "Check geometry" );
}

auto QgsGeometryCheckGapAlgorithm::groupId() const -> QString
{
  return QStringLiteral( "checkgeometry" );
}

auto QgsGeometryCheckGapAlgorithm::shortHelpString() const -> QString
{
  return QObject::tr( "This algorithm check the gaps between polygons." );
}

auto QgsGeometryCheckGapAlgorithm::flags() const -> Qgis::ProcessingAlgorithmFlags
{
  return QgsProcessingAlgorithm::flags() | Qgis::ProcessingAlgorithmFlag::NoThreading;
}

auto QgsGeometryCheckGapAlgorithm::createInstance() const -> QgsGeometryCheckGapAlgorithm *
{
  return new QgsGeometryCheckGapAlgorithm();
}

void QgsGeometryCheckGapAlgorithm::initAlgorithm( const QVariantMap &configuration )
{
  Q_UNUSED( configuration )

  addParameter( new QgsProcessingParameterVectorLayer( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ), QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorPolygon ) ) );
  addParameter( new QgsProcessingParameterNumber( QStringLiteral( "GAP_THRESHOLD" ), QObject::tr( "Gap threshold" ), Qgis::ProcessingNumberParameterType::Double, 0, false, 0.0 ) );

  // Optional allowed gaps layer and buffer value
  addParameter( new QgsProcessingParameterBoolean( QStringLiteral( "ALLOWED_GAPS_ENABLED" ), QObject::tr( "Enable allowed gaps" ), false ) );
  addParameter( new QgsProcessingParameterVectorLayer( QStringLiteral( "ALLOWED_GAPS_LAYER" ), QObject::tr( "Allowed gaps layer" ), QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorPolygon ), QVariant(), true ) );
  addParameter( new QgsProcessingParameterNumber( QStringLiteral( "ALLOWED_GAPS_BUFFER" ), QObject::tr( "Allowed gaps buffer" ), Qgis::ProcessingNumberParameterType::Double, 0, true, 0.0 ) );

  addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "ERRORS" ), QObject::tr( "Errors layer" ), Qgis::ProcessingSourceType::VectorPoint ) );
  addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Output layer" ), Qgis::ProcessingSourceType::VectorPolygon ) );

  std::unique_ptr< QgsProcessingParameterNumber > tolerance = std::make_unique< QgsProcessingParameterNumber >( QStringLiteral( "TOLERANCE" ),
      QObject::tr( "Tolerance" ), Qgis::ProcessingNumberParameterType::Integer, 8, false, 1, 13 );
  tolerance->setFlags( tolerance->flags() | Qgis::ProcessingParameterFlag::Advanced );
  addParameter( tolerance.release() );
}

auto QgsGeometryCheckGapAlgorithm::prepareAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback * ) -> bool
{
  mTolerance = parameterAsInt( parameters, QStringLiteral( "TOLERANCE" ), context );

  return true;
}

auto QgsGeometryCheckGapAlgorithm::createFeaturePool( QgsVectorLayer *layer, bool selectedOnly ) const -> QgsFeaturePool *
{
  return new QgsVectorDataProviderFeaturePool( layer, selectedOnly );
}

static auto outputFields( ) -> QgsFields
{
  QgsFields fields;
  fields.append( QgsField( QStringLiteral( "gc_layerid" ), QVariant::String ) );
  fields.append( QgsField( QStringLiteral( "gc_layername" ), QVariant::String ) );
  fields.append( QgsField( QStringLiteral( "gc_featid" ), QVariant::Int ) );
  fields.append( QgsField( QStringLiteral( "gc_partidx" ), QVariant::Int ) );
  fields.append( QgsField( QStringLiteral( "gc_ringidx" ), QVariant::Int ) );
  fields.append( QgsField( QStringLiteral( "gc_vertidx" ), QVariant::Int ) );
  fields.append( QgsField( QStringLiteral( "gc_errorx" ), QVariant::Double ) );
  fields.append( QgsField( QStringLiteral( "gc_errory" ), QVariant::Double ) );
  fields.append( QgsField( QStringLiteral( "gc_error" ), QVariant::String ) );
  return fields;
}


auto QgsGeometryCheckGapAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) -> QVariantMap
{
  QString dest_output;
  QString dest_errors;
  QgsVectorLayer *inputLayer = parameterAsVectorLayer( parameters, QStringLiteral( "INPUT" ), context );
  QgsVectorLayer *allowedGapsLayer = parameterAsVectorLayer( parameters, QStringLiteral( "ALLOWED_GAPS_LAYER" ), context );
  double allowedGapsBuffer = parameterAsDouble( parameters, QStringLiteral( "ALLOWED_GAPS_BUFFER" ), context );
  double gapThreshold = parameterAsDouble( parameters, QStringLiteral( "GAP_THRESHOLD" ), context );
  bool allowedGapsEnabled = parameterAsBoolean( parameters, QStringLiteral( "ALLOWED_GAPS_ENABLED" ), context );

  if ( allowedGapsEnabled && allowedGapsLayer == nullptr )
  {
    throw QgsProcessingException( QObject::tr( "Allowed gaps enabled, but no allowed gaps layer specified" ) );
  }

  if ( allowedGapsEnabled && !allowedGapsLayer->isValid() )
  {
    throw QgsProcessingException( QObject::tr( "Allowed gaps enabled, but allowed gaps layer is invalid" ) );
  }

  QgsFields fields = outputFields();

  std::unique_ptr< QgsFeatureSink > sink_output( parameterAsSink( parameters, QStringLiteral( "OUTPUT" ), context, dest_output, fields, inputLayer->wkbType(), inputLayer->sourceCrs() ) );
  if ( !sink_output )
  {
    throw QgsProcessingException( invalidSinkError( parameters, QStringLiteral( "OUTPUT" ) ) );
  }
  std::unique_ptr< QgsFeatureSink > sink_errors( parameterAsSink( parameters, QStringLiteral( "ERRORS" ), context, dest_errors, fields, Qgis::WkbType::Point, inputLayer->sourceCrs() ) );
  if ( !sink_errors )
  {
    throw QgsProcessingException( invalidSinkError( parameters, QStringLiteral( "ERRORS" ) ) );
  }

  QgsProcessingMultiStepFeedback multiStepFeedback( 3, feedback );

  QgsProject *project = inputLayer->project() ? inputLayer->project() : QgsProject::instance();

  std::unique_ptr<QgsGeometryCheckContext> checkContext = std::make_unique<QgsGeometryCheckContext>( mTolerance, inputLayer->sourceCrs(), project->transformContext(), project );

  // Test detection
  QList<QgsGeometryCheckError *> checkErrors;
  QStringList messages;

  QVariantMap configurationCheck;
  configurationCheck.insert( "gapThreshold", gapThreshold );
  configurationCheck.insert( "allowedGapsEnabled", allowedGapsEnabled );
  if ( allowedGapsEnabled )
  {
    configurationCheck.insert( "allowedGapsLayer", allowedGapsLayer->id() );
    configurationCheck.insert( "allowedGapsBuffer", allowedGapsBuffer );
  }
  const QgsGeometryGapCheck check( checkContext.get(), configurationCheck );

  multiStepFeedback.setCurrentStep( 1 );
  feedback->setProgressText( QObject::tr( "Preparing features…" ) );
  QMap<QString, QgsFeaturePool *> featurePools;
  featurePools.insert( inputLayer->id(), createFeaturePool( inputLayer ) );
  if ( allowedGapsEnabled )
    featurePools.insert( allowedGapsLayer->id(), createFeaturePool( allowedGapsLayer ) );

  multiStepFeedback.setCurrentStep( 2 );
  feedback->setProgressText( QObject::tr( "Collecting errors…" ) );
  check.collectErrors( featurePools, checkErrors, messages, feedback );

  multiStepFeedback.setCurrentStep( 3 );
  feedback->setProgressText( QObject::tr( "Exporting errors…" ) );
  double step{checkErrors.size() > 0 ? 100.0 / checkErrors.size() : 1};
  long i = 0;
  feedback->setProgress( 0.0 );

  for ( QgsGeometryCheckError *error : checkErrors )
  {

    if ( feedback->isCanceled() )
    {
      break;
    }
    QgsFeature f;
    QgsAttributes attrs = f.attributes();

    attrs << inputLayer->id()
          << inputLayer->name()
          << QVariant()
          << error->vidx().part
          << error->vidx().ring
          << error->vidx().vertex
          << error->location().x()
          << error->location().y()
          << error->value().toString();
    f.setAttributes( attrs );

    f.setGeometry( error->geometry() );
    if ( !sink_output->addFeature( f, QgsFeatureSink::FastInsert ) )
      throw QgsProcessingException( writeFeatureError( sink_output.get(), parameters, QStringLiteral( "OUTPUT" ) ) );

    f.setGeometry( QgsGeometry::fromPoint( QgsPoint( error->location().x(), error->location().y() ) ) );
    if ( !sink_errors->addFeature( f, QgsFeatureSink::FastInsert ) )
      throw QgsProcessingException( writeFeatureError( sink_errors.get(), parameters, QStringLiteral( "ERRORS" ) ) );

    i++;
    feedback->setProgress( 100.0 * step * static_cast<double>( i ) );
  }

  QVariantMap outputs;
  outputs.insert( QStringLiteral( "OUTPUT" ), dest_output );
  outputs.insert( QStringLiteral( "ERRORS" ), dest_errors );

  return outputs;
}

///@endcond
