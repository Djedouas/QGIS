/***************************************************************************
                        qgsalgorithmcheckgeometryisvalid.cpp
                        ---------------------
   begin                : March 2024
   copyright            : (C) 2024 by Jacky Volpes
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

#include "qgsalgorithmcheckgeometryisvalid.h"
#include "qgsgeometrycheckcontext.h"
#include "qgsgeometrycheckerror.h"
#include "qgsgeometryisvalidcheck.h"
#include "qgspoint.h"
#include "qgsvectorlayer.h"
#include "qgsvectordataproviderfeaturepool.h"

///@cond PRIVATE

auto QgsGeometryCheckIsValidAlgorithm::name() const -> QString
{
  return QStringLiteral( "checkgeometryisvalid" );
}

auto QgsGeometryCheckIsValidAlgorithm::displayName() const -> QString
{
  return QObject::tr( "Check Geometry (is valid)" );
}

auto QgsGeometryCheckIsValidAlgorithm::tags() const -> QStringList
{
  return QObject::tr( "check,geometry,validation" ).split( ',' );
}

auto QgsGeometryCheckIsValidAlgorithm::group() const -> QString
{
  return QObject::tr( "Check geometry" );
}

auto QgsGeometryCheckIsValidAlgorithm::groupId() const -> QString
{
  return QStringLiteral( "checkgeometry" );
}

auto QgsGeometryCheckIsValidAlgorithm::shortHelpString() const -> QString
{
  return QObject::tr( "This algorithm check the validity of geometry." );
}

auto QgsGeometryCheckIsValidAlgorithm::flags() const -> Qgis::ProcessingAlgorithmFlags
{
  return QgsProcessingAlgorithm::flags() | Qgis::ProcessingAlgorithmFlag::NoThreading;
}

auto QgsGeometryCheckIsValidAlgorithm::createInstance() const -> QgsGeometryCheckIsValidAlgorithm *
{
  return new QgsGeometryCheckIsValidAlgorithm();
}

void QgsGeometryCheckIsValidAlgorithm::initAlgorithm( const QVariantMap &configuration )
{
  Q_UNUSED( configuration )

  addParameter( new QgsProcessingParameterVectorLayer( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ), QList<int>() << static_cast<int>( Qgis::ProcessingSourceType::VectorPolygon ) << static_cast<int>( Qgis::ProcessingSourceType::VectorLine ) ) );
  addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "ERRORS" ), QObject::tr( "Errors layer" ), Qgis::ProcessingSourceType::VectorPoint ) );
  addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "OUTPUT" ), QObject::tr( "Output layer" ), Qgis::ProcessingSourceType::VectorAnyGeometry ) );

  addParameter( new QgsProcessingParameterEnum( QStringLiteral( "METHOD" ), QObject::tr( "Method" ),
                QStringList() << QObject::tr( "Use global digitizing settings" )
                << QStringLiteral( "QGIS" )
                << QStringLiteral( "GEOS" ),
                false, 0 )
              );
  addParameter( new QgsProcessingParameterEnum( QStringLiteral( "ALLOW_SELF_TOUCHING_HOLES" ), QObject::tr( "Allow self touching holes" ),
                QStringList() << QObject::tr( "Use global digitizing settings" )
                << QObject::tr( "Yes" )
                << QObject::tr( "No" ),
                false, 0 ) );

  std::unique_ptr< QgsProcessingParameterNumber > tolerance = std::make_unique< QgsProcessingParameterNumber >( QStringLiteral( "TOLERANCE" ),
      QObject::tr( "Tolerance" ), Qgis::ProcessingNumberParameterType::Integer, 8, false, 1, 13 );
  tolerance->setFlags( tolerance->flags() | Qgis::ProcessingParameterFlag::Advanced );
  addParameter( tolerance.release() );
}

auto QgsGeometryCheckIsValidAlgorithm::prepareAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback * ) -> bool
{
  mTolerance = parameterAsInt( parameters, QStringLiteral( "TOLERANCE" ), context );

  return true;
}

auto QgsGeometryCheckIsValidAlgorithm::createFeaturePool( QgsVectorLayer *layer, bool selectedOnly ) const -> QgsFeaturePool *
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


auto QgsGeometryCheckIsValidAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) -> QVariantMap
{
  QString dest_output;
  QString dest_errors;
  QgsVectorLayer *inputLayer = parameterAsVectorLayer( parameters, QStringLiteral( "INPUT" ), context );

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

  int method = parameterAsEnum( parameters, QStringLiteral( "METHOD" ), context );
  int allowSelfTouchingHoles = parameterAsEnum( parameters, QStringLiteral( "ALLOW_SELF_TOUCHING_HOLES" ), context );

  QVariantMap configurationCheck;
  if ( method != 0 )
    configurationCheck.insert( "method", method );
  if ( allowSelfTouchingHoles != 0 )
    configurationCheck.insert( "allowSelfTouchingHoles", allowSelfTouchingHoles == 1 );

  const QgsGeometryIsValidCheck check( checkContext.get(), configurationCheck );

  multiStepFeedback.setCurrentStep( 1 );
  feedback->setProgressText( QObject::tr( "Preparing features…" ) );
  QMap<QString, QgsFeaturePool *> featurePools;
  featurePools.insert( inputLayer->id(), createFeaturePool( inputLayer ) );

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
    QgsGeometryCheckErrorSingle *singleCheckError = dynamic_cast<QgsGeometryCheckErrorSingle *>( error );
    if ( singleCheckError == nullptr )
      continue;

    if ( feedback->isCanceled() )
      break;

    QgsFeature f;
    QgsAttributes attrs = f.attributes();

    attrs << singleCheckError->layerId()
          << inputLayer->name()
          << singleCheckError->featureId()
          << singleCheckError->vidx().part
          << singleCheckError->vidx().ring
          << singleCheckError->vidx().vertex
          << singleCheckError->location().x()
          << singleCheckError->location().y()
          << singleCheckError->singleError()->description();
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
