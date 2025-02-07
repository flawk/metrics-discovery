/*========================== begin_copyright_notice ============================

Copyright (C) 2019-2021 Intel Corporation

SPDX-License-Identifier: MIT

============================= end_copyright_notice ===========================*/

//     File Name:  md_calculation.cpp

//     Abstract:   C++ metrics discovery metrics calculation features implementation.

#include "md_calculation.h"
#include "md_adapter.h"
#include "md_equation.h"
#include "md_information.h"
#include "md_metric.h"
#include "md_metric_set.h"
#include "md_metrics_device.h"
#include "md_types.h"

#include <algorithm>
#include <cstring>

namespace MetricsDiscoveryInternal
{
    // Forward declarations //
    template <>
    int32_t CMetricsCalculationManager<MEASUREMENT_TYPE_SNAPSHOT_IO>::GetInformationIndex( const char* symbolName, CMetricSet* metricSet );

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculationManager<MEASUREMENT_TYPE_SNAPSHOT_IO>
    //
    // Method:
    //     ResetContext
    //
    // Description:
    //     Resets IoStream calculation context to the initial (zero) state.
    //
    // Input:
    //     TCalculationContext& context - (IN/OUT) calculation context
    //
    //////////////////////////////////////////////////////////////////////////////
    template <>
    void CMetricsCalculationManager<MEASUREMENT_TYPE_SNAPSHOT_IO>::ResetContext( TCalculationContext& context )
    {
        memset( &context.StreamCalculationContext, 0, sizeof( context.StreamCalculationContext ) );

        context.StreamCalculationContext.ContextIdIdx = -1;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculationManager<MEASUREMENT_TYPE_DELTA_QUERY>
    //
    // Method:
    //     ResetContext
    //
    // Description:
    //     Resets Query calculation context to the initial (zero) state.
    //
    // Input:
    //     TCalculationContext& context - (IN/OUT) calculation context
    //
    //////////////////////////////////////////////////////////////////////////////
    template <>
    void CMetricsCalculationManager<MEASUREMENT_TYPE_DELTA_QUERY>::ResetContext( TCalculationContext& context )
    {
        memset( &context.QueryCalculationContext, 0, sizeof( context.QueryCalculationContext ) );
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculationManager<MEASUREMENT_TYPE_SNAPSHOT_IO>
    //
    // Method:
    //     PrepareContext
    //
    // Description:
    //     Prepares IoStream calculation context for calculations. Sets all the necessary
    //     fields that could be determined using user provided ones.
    //
    // Input:
    //     TCalculationContext& context - (IN/OUT) calculation context
    //
    // Output:
    //     TCompletionCode              - *CC_OK* means success
    //
    //////////////////////////////////////////////////////////////////////////////
    template <>
    TCompletionCode CMetricsCalculationManager<MEASUREMENT_TYPE_SNAPSHOT_IO>::PrepareContext( TCalculationContext& context )
    {
        TStreamCalculationContext* sc = &context.StreamCalculationContext;
        MD_CHECK_PTR_RET( sc->Calculator, CC_ERROR_INVALID_PARAMETER );

        const uint32_t adapterId = OBTAIN_ADAPTER_ID( sc->Calculator->GetMetricsDevice() );

        MD_CHECK_PTR_RET_A( adapterId, sc->MetricSet, CC_ERROR_INVALID_PARAMETER );
        MD_CHECK_PTR_RET_A( adapterId, sc->RawData, CC_ERROR_INVALID_PARAMETER );
        MD_CHECK_PTR_RET_A( adapterId, sc->Out, CC_ERROR_INVALID_PARAMETER );
        MD_CHECK_PTR_RET_A( adapterId, sc->DeltaValues, CC_ERROR_INVALID_PARAMETER );

        // Find required indices for context filtering, report filtering and PreviousContextId information
        sc->ContextIdIdx    = GetInformationIndex( "ContextId", sc->MetricSet );
        sc->ReportReasonIdx = GetInformationIndex( "ReportReason", sc->MetricSet );

        if( sc->DoContextFiltering )
        {
            if( sc->ContextIdIdx < 0 )
            {
                MD_LOG_A( adapterId, LOG_ERROR, "error: can't find required information for context filtering" );
                MD_LOG_EXIT_A( adapterId );
                return CC_ERROR_INVALID_PARAMETER;
            }
        }

        sc->MetricsAndInformationCount = sc->MetricSet->GetParams()->MetricsCount + sc->MetricSet->GetParams()->InformationCount;
        sc->RawReportSize              = sc->MetricSet->GetParams()->RawReportSize;

        sc->OutReportCount      = 0;
        sc->OutPtr              = sc->Out;
        sc->OutMaxValuesPtr     = sc->OutMaxValues;
        sc->PrevRawDataPtr      = sc->RawData;
        sc->PrevRawReportNumber = 0;
        sc->LastRawDataPtr      = sc->RawData;
        sc->LastRawReportNumber = 0;

        sc->Calculator->Reset( sc->RawReportSize );

        return CC_OK;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculationManager<MEASUREMENT_TYPE_DELTA_QUERY>
    //
    // Method:
    //     PrepareContext
    //
    // Description:
    //     Prepares Query calculation context for calculations. Sets all the necessary
    //     fields that could be determined using user provided ones.
    //
    // Input:
    //     TCalculationContext& context - (IN/OUT) calculation context
    //
    // Output:
    //     TCompletionCode              - *CC_OK* means success
    //
    //////////////////////////////////////////////////////////////////////////////
    template <>
    TCompletionCode CMetricsCalculationManager<MEASUREMENT_TYPE_DELTA_QUERY>::PrepareContext( TCalculationContext& context )
    {
        TQueryCalculationContext* qc = &context.QueryCalculationContext;
        MD_CHECK_PTR_RET( qc->Calculator, CC_ERROR_INVALID_PARAMETER );

        const uint32_t adapterId = OBTAIN_ADAPTER_ID( qc->Calculator->GetMetricsDevice() );

        MD_CHECK_PTR_RET_A( adapterId, qc->MetricSet, CC_ERROR_INVALID_PARAMETER );
        MD_CHECK_PTR_RET_A( adapterId, qc->RawData, CC_ERROR_INVALID_PARAMETER );
        MD_CHECK_PTR_RET_A( adapterId, qc->Out, CC_ERROR_INVALID_PARAMETER );
        MD_CHECK_PTR_RET_A( adapterId, qc->DeltaValues, CC_ERROR_INVALID_PARAMETER );

        qc->Calculator->Reset();

        qc->MetricsAndInformationCount = qc->MetricSet->GetParams()->MetricsCount + qc->MetricSet->GetParams()->InformationCount;
        qc->RawReportSize              = qc->MetricSet->GetParams()->QueryReportSize;

        qc->OutReportCount  = 0;
        qc->OutPtr          = qc->Out;
        qc->OutMaxValuesPtr = qc->OutMaxValues;
        qc->RawDataPtr      = qc->RawData;

        return CC_OK;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculationManager<MEASUREMENT_TYPE_SNAPSHOT_IO>
    //
    // Method:
    //     CalculateNextReport
    //
    // Description:
    //     Calculates a single report for a IoStream measurements using raw data and
    //     other state variables stored in the given calculation context.
    //     If context filtering is enabled calculation is performed only if starting raw report
    //     is from appropriate context id.
    //
    // Input:
    //     TCalculationContext& context - (IN/OUT) calculation context
    //
    // Output:
    //     bool - true, if report calculated
    //            false, if not - calculation complete for current context
    //
    //////////////////////////////////////////////////////////////////////////////
    template <>
    bool CMetricsCalculationManager<MEASUREMENT_TYPE_SNAPSHOT_IO>::CalculateNextReport( TCalculationContext& context )
    {
        TStreamCalculationContext* sc = &context.StreamCalculationContext;
        MD_CHECK_PTR_RET( sc->Calculator, false );

        const uint32_t adapterId = OBTAIN_ADAPTER_ID( sc->Calculator->GetMetricsDevice() );

        if( sc->LastRawReportNumber >= sc->RawReportCount || sc->PrevRawReportNumber >= sc->RawReportCount - 1 )
        {
            // Nothing to be calculated
            MD_LOG_A( adapterId, LOG_DEBUG, "Calculation complete" );
            if( CC_OK != sc->Calculator->SaveReport( sc->LastRawDataPtr ) )
            {
                MD_LOG_A( adapterId, LOG_DEBUG, "Unable to store last raw report for reuse." );
            }

            return false;
        }

        if( sc->Calculator->SavedReportPresent() && sc->PrevRawReportNumber == 0 )
        {
            // Use saved report as 'Prev', 0 offset report as "Last"
            sc->PrevRawDataPtr      = sc->Calculator->GetSavedReport();
            sc->PrevRawReportNumber = MD_SAVED_REPORT_NUMBER;
            MD_ASSERT_A( adapterId, sc->PrevRawDataPtr != nullptr );
        }

        // If not using saved report
        if( sc->PrevRawReportNumber != MD_SAVED_REPORT_NUMBER )
        {
            sc->LastRawDataPtr      = sc->PrevRawDataPtr + sc->RawReportSize;
            sc->LastRawReportNumber = sc->PrevRawReportNumber + 1;
        }

        // METRICS
        sc->Calculator->ReadMetricsFromIoReport( sc->LastRawDataPtr, sc->PrevRawDataPtr, sc->DeltaValues, *sc->MetricSet );
        // NORMALIZATION
        sc->Calculator->NormalizeMetrics( sc->DeltaValues, sc->OutPtr, *sc->MetricSet );
        // INFORMATION
        sc->Calculator->ReadInformation( sc->LastRawDataPtr, sc->OutPtr + sc->MetricSet->GetParams()->MetricsCount, *sc->MetricSet, sc->ContextIdIdx );
        // MAX VALUES
        if( sc->OutMaxValues )
        {
            sc->Calculator->CalculateMaxValues( sc->DeltaValues, sc->OutPtr, sc->OutMaxValuesPtr, *sc->MetricSet );
            sc->OutMaxValuesPtr += sc->MetricSet->GetParams()->MetricsCount;
        }

        sc->OutPtr += sc->MetricsAndInformationCount;
        sc->OutReportCount++;

        // Prev is now Last
        sc->PrevRawDataPtr      = sc->LastRawDataPtr;
        sc->PrevRawReportNumber = sc->LastRawReportNumber;

        if( sc->Calculator->SavedReportPresent() )
        {
            sc->Calculator->DiscardSavedReport();
        }

        return true;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculationManager<MEASUREMENT_TYPE_DELTA_QUERY>
    //
    // Method:
    //     CalculateNextReport
    //
    // Description:
    //     Calculates a single report for a Query measurements using raw delta data and
    //     other state variables stored in the given calculation context.
    //
    // Input:
    //     TCalculationContext& context - (IN/OUT) calculation context
    //
    // Output:
    //     bool - true, if report calculated
    //            false, if not - calculation complete for current context
    //
    //////////////////////////////////////////////////////////////////////////////
    template <>
    bool CMetricsCalculationManager<MEASUREMENT_TYPE_DELTA_QUERY>::CalculateNextReport( TCalculationContext& context )
    {
        TQueryCalculationContext* qc = &context.QueryCalculationContext;
        MD_CHECK_PTR_RET( qc->Calculator, false );

        const uint32_t adapterId = OBTAIN_ADAPTER_ID( qc->Calculator->GetMetricsDevice() );

        if( qc->OutReportCount >= qc->RawReportCount )
        {
            // Nothing to be calculated
            MD_LOG_A( adapterId, LOG_DEBUG, "Calculation complete" );
            return false;
        }

        // METRICS
        qc->Calculator->ReadMetricsFromQueryReport( qc->RawDataPtr, qc->DeltaValues, *qc->MetricSet );
        // NORMALIZATION
        qc->Calculator->NormalizeMetrics( qc->DeltaValues, qc->OutPtr, *qc->MetricSet );
        // INFORMATION
        qc->Calculator->ReadInformation( qc->RawDataPtr, qc->OutPtr + qc->MetricSet->GetParams()->MetricsCount, *qc->MetricSet, -1 );
        // MAX VALUES
        if( qc->OutMaxValues )
        {
            qc->Calculator->CalculateMaxValues( qc->DeltaValues, qc->OutPtr, qc->OutMaxValuesPtr, *qc->MetricSet );
            qc->OutMaxValuesPtr += qc->MetricSet->GetParams()->MetricsCount;
        }

        qc->RawDataPtr += qc->RawReportSize;
        qc->OutPtr += qc->MetricsAndInformationCount;

        qc->OutReportCount++;

        return true;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculationManager<MEASUREMENT_TYPE_SNAPSHOT_IO>
    //
    // Method:
    //     GetInformationIndex
    //
    // Description:
    //     Returns the given information index in the given set. -1 if not found.
    //
    // Input:
    //     const char* symbolName - information symbol name to find
    //     CMetricSet* metricSet  - metric set
    //
    // Output:
    //     int - given information index in MetricSet, -1 if not found or error
    //
    //////////////////////////////////////////////////////////////////////////////
    template <>
    int CMetricsCalculationManager<MEASUREMENT_TYPE_SNAPSHOT_IO>::GetInformationIndex( const char* symbolName, CMetricSet* metricSet )
    {
        MD_CHECK_PTR_RET( metricSet, -1 );

        const uint32_t adapterId = OBTAIN_ADAPTER_ID( metricSet->GetMetricsDevice() );

        MD_CHECK_PTR_RET_A( adapterId, symbolName, -1 );

        uint32_t count = metricSet->GetParams()->InformationCount;
        for( uint32_t i = 0; i < count; i++ )
        {
            IInformation_1_0* information = metricSet->GetInformation( i );
            MD_ASSERT_A( adapterId, information != nullptr );

            TInformationParams_1_0* informationParams = information->GetParams();
            if( informationParams->SymbolName && strcmp( informationParams->SymbolName, symbolName ) == 0 )
            {
                return i;
            }
        }

        MD_LOG_A( adapterId, LOG_DEBUG, "can't find information index: %s", symbolName );
        return -1;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     CMetricsCalculator
    //
    // Description:
    //     CMetricsCalculator constructor.
    //
    // Input:
    //     CMetricsDevice* metricsDevice - MetricsDevice used for obtaining GlobalSymbols
    //                                     during calculations
    //
    //////////////////////////////////////////////////////////////////////////////
    CMetricsCalculator::CMetricsCalculator( CMetricsDevice* metricsDevice )
    {
        m_device             = metricsDevice;
        m_gpuCoreClocks      = 0;
        m_euCoresCount       = 0;
        m_savedReport        = nullptr;
        m_savedReportSize    = 0;
        m_contextIdPrev      = 0;
        m_savedReportPresent = false;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     ~CMetricsCalculator
    //
    // Description:
    //     CMetricsCalculator destructor.
    //
    //////////////////////////////////////////////////////////////////////////////
    CMetricsCalculator::~CMetricsCalculator()
    {
        MD_SAFE_DELETE_ARRAY( m_savedReport );
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //    CMetricsCalculator
    //
    // Method:
    //     Reset
    //
    // Description:
    //     Reset Calculator to the initial state.
    //     Allocate memory to store last raw report for future calculation.
    //
    // Input:
    //     uint32_t rawReportSize - Raw report size to allocate memory for report to save.
    //
    //////////////////////////////////////////////////////////////////////////////
    void CMetricsCalculator::Reset( uint32_t rawReportSize /* = 0*/ )
    {
        TTypedValue_1_0* euCoresTotalCount = GetGlobalSymbolValue( "EuCoresTotalCount" );
        // Workaround for renamed EuCoresTotalCount
        if( euCoresTotalCount == nullptr )
        {
            euCoresTotalCount = GetGlobalSymbolValue( "VectorEngineTotalCount" );
        }
        m_euCoresCount  = euCoresTotalCount ? euCoresTotalCount->ValueUInt32 : 0;
        m_gpuCoreClocks = 0;

        if( m_savedReportSize != rawReportSize && rawReportSize > 0 )
        {
            MD_SAFE_DELETE_ARRAY( m_savedReport );
            m_savedReport = new( std::nothrow ) uint8_t[rawReportSize];
            if( m_savedReport == nullptr )
            {
                MD_LOG_A( m_device->GetAdapter().GetAdapterId(), LOG_ERROR, "error allocating saved report memory" );
                m_savedReportSize = 0;
            }
            else
            {
                m_savedReportSize = rawReportSize;
            }
            m_savedReportPresent = false;
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //    CMetricsCalculator
    //
    // Method:
    //     SaveReport
    //
    // Description:
    //     Stores report for next calculations
    //
    // Input:
    //     const uint8_t* reportToSave - (IN) single raw report to save
    //
    // Output:
    //     TCompletionCode - CC_OK on success
    //
    //////////////////////////////////////////////////////////////////////////////
    TCompletionCode CMetricsCalculator::SaveReport( const uint8_t* reportToSave )
    {
        const uint32_t adapterId = OBTAIN_ADAPTER_ID( m_device );

        MD_CHECK_PTR_RET_A( adapterId, m_savedReport, CC_ERROR_INVALID_PARAMETER );
        MD_CHECK_PTR_RET_A( adapterId, reportToSave, CC_ERROR_INVALID_PARAMETER );

        bool res = iu_memcpy_s( m_savedReport, m_savedReportSize, reportToSave, m_savedReportSize );
        if( res )
        {
            m_savedReportPresent = true;
        }

        return res ? CC_OK : CC_ERROR_GENERAL;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //    CMetricsCalculator
    //
    // Method:
    //     SavedReportPresent
    //
    // Description:
    //     Check if Calculator has saved report.
    //
    // Output:
    //     bool - true if report saved
    //
    //////////////////////////////////////////////////////////////////////////////
    bool CMetricsCalculator::SavedReportPresent()
    {
        return m_savedReportPresent;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //    CMetricsCalculator
    //
    // Method:
    //     GetSavedReport
    //
    // Description:
    //     Getter for saved report pointer.
    //
    // Output:
    //     uint8_t* - saved report
    //
    //////////////////////////////////////////////////////////////////////////////
    uint8_t* CMetricsCalculator::GetSavedReport()
    {
        return m_savedReport;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //    CMetricsCalculator
    //
    // Method:
    //     DiscardSavedReport
    //
    // Description:
    //     Sets to false flag indicating report save.
    //
    //////////////////////////////////////////////////////////////////////////////
    void CMetricsCalculator::DiscardSavedReport()
    {
        m_savedReportPresent = false;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     ReadMetricsFromQueryReport
    //
    // Description:
    //     Reads metrics from a given metric set using raw report data.
    //
    // Input:
    //     const uint8_t*   rawReport - (IN) single raw report
    //     TTypedValue_1_0* outValues - (OUT) single output report
    //     CMetricSet&      metricSet - metric set for which the calculation will be conducted
    //
    // Output:
    //     TCompletionCode - *CC_OK* means success
    //
    //////////////////////////////////////////////////////////////////////////////
    TCompletionCode CMetricsCalculator::ReadMetricsFromQueryReport( const uint8_t* rawReport, TTypedValue_1_0* outValues, CMetricSet& metricSet )
    {
        const uint32_t adapterId = OBTAIN_ADAPTER_ID( m_device );

        MD_CHECK_PTR_RET_A( adapterId, rawReport, CC_ERROR_INVALID_PARAMETER );
        MD_CHECK_PTR_RET_A( adapterId, outValues, CC_ERROR_INVALID_PARAMETER );

        m_gpuCoreClocks = 0;

        uint32_t metricsCount = metricSet.GetParams()->MetricsCount;
        for( uint32_t i = 0; i < metricsCount; i++ )
        {
            TMetricParams_1_0* metricParams = metricSet.GetMetric( i )->GetParams();

            if( metricParams->QueryReadEquation )
            {
                CEquation& equation = static_cast<CEquation&>( *metricParams->QueryReadEquation );
                outValues[i]        = CalculateReadEquation( equation, rawReport );
            }
            else
            {
                outValues[i].ValueType   = VALUE_TYPE_UINT64;
                outValues[i].ValueUInt64 = 0ULL;
            }

            if( strcmp( metricParams->SymbolName, "GpuCoreClocks" ) == 0 )
            {
                m_gpuCoreClocks = outValues[i].ValueUInt64;
            }
        }

        return CC_OK;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     ReadMetricsFromIoReport
    //
    // Description:
    //     Reads metrics from a given metric set using raw report data for prev and last report.
    //
    // Input:
    //     const uint8_t*   rawRaportLast - (IN) last (next) single raw report
    //     const uint8_t*   rawRaportPrev - (IN) previous single raw report
    //     TTypedValue_1_0* outValues     - (OUT) read metric values
    //     CMetricSet&      metricSet     - MetricSet for calculations
    //
    // Output:
    //     TCompletionCode - *CC_OK* means success
    //
    //////////////////////////////////////////////////////////////////////////////
    TCompletionCode CMetricsCalculator::ReadMetricsFromIoReport( const uint8_t* rawRaportLast, const uint8_t* rawRaportPrev, TTypedValue_1_0* outValues, CMetricSet& metricSet )
    {
        const uint32_t adapterId = OBTAIN_ADAPTER_ID( m_device );

        MD_CHECK_PTR_RET_A( adapterId, rawRaportLast, CC_ERROR_INVALID_PARAMETER );
        MD_CHECK_PTR_RET_A( adapterId, rawRaportPrev, CC_ERROR_INVALID_PARAMETER );
        MD_CHECK_PTR_RET_A( adapterId, outValues, CC_ERROR_INVALID_PARAMETER );

        uint32_t metricsCount = metricSet.GetParams()->MetricsCount;
        m_gpuCoreClocks       = 0;

        for( uint32_t i = 0; i < metricsCount; i++ )
        {
            TMetricParams_1_0* metricParams = metricSet.GetMetric( i )->GetParams();

            if( metricParams->IoReadEquation )
            {
                CEquation&         equationInternal = static_cast<CEquation&>( *metricParams->IoReadEquation );
                TDeltaFunction_1_0 deltaFunc        = metricParams->DeltaFunction;
                outValues[i]                        = CalculateReadEquationAndDelta( equationInternal, deltaFunc, rawRaportLast, rawRaportPrev );
            }
            else
            {
                outValues[i].ValueType   = VALUE_TYPE_UINT64;
                outValues[i].ValueUInt64 = 0ULL;
            }

            if( strcmp( metricParams->SymbolName, "GpuCoreClocks" ) == 0 )
            {
                m_gpuCoreClocks = outValues[i].ValueUInt64;
            }
        }

        return CC_OK;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     NormalizeMetrics
    //
    // Description:
    //     Normalizes metrics from a given metric set using previously read data.
    //
    // Input:
    //     TTypedValue_1_0* deltaValues - (IN) previously read metric delta values
    //     TTypedValue_1_0* outValues   - (OUT) output normalized metric values
    //     CMetricSet&      metricSet   - MetricSet for calculations
    //
    //////////////////////////////////////////////////////////////////////////////
    void CMetricsCalculator::NormalizeMetrics( TTypedValue_1_0* deltaValues, TTypedValue_1_0* outValues, CMetricSet& metricSet )
    {
        const uint32_t adapterId = OBTAIN_ADAPTER_ID( m_device );

        if( !deltaValues || !outValues )
        {
            MD_ASSERT_A( adapterId, deltaValues != nullptr );
            MD_ASSERT_A( adapterId, outValues != nullptr );
            MD_LOG_A( adapterId, LOG_ERROR, "error: nullptr params" );
            return;
        }

        uint32_t metricsCount = metricSet.GetParams()->MetricsCount;

        for( uint32_t i = 0; i < metricsCount; i++ )
        {
            CMetric*           metric                = static_cast<CMetric*>( metricSet.GetMetric( i ) );
            TMetricParams_1_0* metricParams          = metric->GetParams();
            IEquation_1_0*     normalizationEquation = metricParams->NormEquation;

            if( normalizationEquation )
            {
                // do final calculation, may refer to global symbols, local delta results and local normalization results
                CEquation& equationInternal = static_cast<CEquation&>( *normalizationEquation );
                outValues[i]                = CalculateLocalNormalizationEquation( equationInternal, deltaValues, outValues, i );
            }
            else
            {
                outValues[i] = deltaValues[i];
            }

            switch( metricParams->ResultType )
            {
                case RESULT_UINT32:
                    if( outValues[i].ValueType != VALUE_TYPE_UINT32 )
                    {
                        outValues[i].ValueUInt32 = CastToUInt32( outValues[i] );
                        outValues[i].ValueType   = VALUE_TYPE_UINT32;
                    }
                    break;

                case RESULT_UINT64:
                    if( outValues[i].ValueType != VALUE_TYPE_UINT64 )
                    {
                        outValues[i].ValueUInt64 = CastToUInt64( outValues[i] );
                        outValues[i].ValueType   = VALUE_TYPE_UINT64;
                    }
                    break;

                case RESULT_FLOAT:
                    if( outValues[i].ValueType != VALUE_TYPE_FLOAT )
                    {
                        outValues[i].ValueFloat = CastToFloat( outValues[i] );
                        outValues[i].ValueType  = VALUE_TYPE_FLOAT;
                    }
                    break;

                case RESULT_BOOL:
                    if( outValues[i].ValueType != VALUE_TYPE_BOOL )
                    {
                        outValues[i].ValueBool = CastToBoolean( outValues[i] );
                        outValues[i].ValueType = VALUE_TYPE_BOOL;
                    }
                    break;

                default:
                    MD_ASSERT_A( adapterId, false );
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     ReadInformation
    //
    // Description:
    //     Reads information from a given metric set.
    //
    // Input:
    //     const uint8_t*   rawData      - (IN) single raw report data
    //     TTypedValue_1_0* outValues    - (OUT) out values with calculated information
    //     CMetricSet&      metricSet    - MetricSet for calculations
    //     int32_t          contextIdIdx - index of contextId information to cache the value
    //
    //////////////////////////////////////////////////////////////////////////////
    void CMetricsCalculator::ReadInformation( const uint8_t* rawData, TTypedValue_1_0* outValues, CMetricSet& metricSet, int32_t contextIdIdx )
    {
        if( !rawData || !outValues )
        {
            const uint32_t adapterId = OBTAIN_ADAPTER_ID( m_device );

            MD_ASSERT_A( adapterId, rawData != nullptr );
            MD_ASSERT_A( adapterId, outValues != nullptr );
            MD_LOG_A( adapterId, LOG_ERROR, "error: nullptr params" );
            return;
        }

        uint32_t informationCount = metricSet.GetParams()->InformationCount;

        for( uint32_t i = 0; i < informationCount; i++ )
        {
            IInformation_1_0* information = metricSet.GetInformation( i );
            const uint32_t    apiMask     = metricSet.GetParams()->ApiMask;
            ReadSingleInformation( rawData, information, apiMask, &outValues[i] );
        }
        if( contextIdIdx != -1 )
        {
            // Value stored to handle PreviousContextId information and context filtering
            m_contextIdPrev = outValues[contextIdIdx].ValueUInt64;
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     ReadContextIdInformation
    //
    // Description:
    //     Reads contextId information to store it.
    //     Done only in Stream.
    //
    // Input:
    //     const uint8_t* rawData      - (IN) single raw report data
    //     CMetricSet&    metricSet    - MetricSet for calculations
    //     int32_t        contextIdIdx - index of contextId information
    //
    //////////////////////////////////////////////////////////////////////////////
    void CMetricsCalculator::ReadContextIdInformation( const uint8_t* rawData, CMetricSet& metricSet, int32_t contextIdIdx )
    {
        if( contextIdIdx == -1 )
        {
            m_contextIdPrev = 0;
        }
        else
        {
            TTypedValue_1_0   outValue    = {};
            IInformation_1_0* information = metricSet.GetInformation( contextIdIdx );
            const uint32_t    apiMask     = metricSet.GetParams()->ApiMask;
            ReadSingleInformation( rawData, information, apiMask, &outValue );
            m_contextIdPrev = outValue.ValueUInt64;
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     ReadInformationByIndex
    //
    // Description:
    //     Reads information by given index as uint64_t.
    //
    // Input:
    //     const uint8_t* rawData        - (IN) single raw report data
    //     CMetricSet&    metricSet      - MetricSet for calculations
    //     int32_t        informationIdx - index of information
    //
    // Output:
    //     uint64_t - Information value in uint64_t format
    //
    //////////////////////////////////////////////////////////////////////////////
    uint64_t CMetricsCalculator::ReadInformationByIndex( const uint8_t* rawData, CMetricSet& metricSet, int32_t informationIndex )
    {
        if( informationIndex == -1 )
        {
            return 0;
        }
        else
        {
            TTypedValue_1_0   outValue    = {};
            IInformation_1_0* information = metricSet.GetInformation( informationIndex );
            const uint32_t    apiMask     = metricSet.GetParams()->ApiMask;
            ReadSingleInformation( rawData, information, apiMask, &outValue );
            return outValue.ValueUInt64;
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     ReadIoMeasurementInformation
    //
    // Description:
    //     Calculates IoMeasurementInformation obtained on every ReadIoStream.
    //
    // Input:
    //     IConcurrentGroup_1_1& concurrentGroup - concurrentGroup which was used during ReadIoStream
    //     TTypedValue_1_0*      outValues       - (OUT) calculated values
    //
    //////////////////////////////////////////////////////////////////////////////
    void CMetricsCalculator::ReadIoMeasurementInformation( IConcurrentGroup_1_1& concurrentGroup, TTypedValue_1_0* outValues )
    {
        const uint32_t adapterId = OBTAIN_ADAPTER_ID( m_device );

        if( !outValues )
        {
            MD_ASSERT_A( adapterId, outValues != nullptr );
            MD_LOG_A( adapterId, LOG_ERROR, "ERROR: outValues is nullptr" );
            return;
        }

        for( uint32_t i = 0; i < concurrentGroup.GetParams()->IoMeasurementInformationCount; ++i )
        {
            auto measurementInfo = concurrentGroup.GetIoMeasurementInformation( i );
            MD_ASSERT_A( adapterId, measurementInfo != nullptr );

            auto equation = measurementInfo->GetParams()->IoReadEquation;
            if( equation )
            {
                outValues[i] = CalculateReadEquation( static_cast<CEquation&>( *equation ), nullptr );
            }
            else
            {
                outValues[i].ValueUInt64 = 0ULL;
            }

            if( measurementInfo->GetParams()->InfoType == INFORMATION_TYPE_FLAG )
            {
                outValues[i].ValueType = VALUE_TYPE_BOOL;
            }
            else
            {
                outValues[i].ValueType = VALUE_TYPE_UINT64;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     CalculateMaxValues
    //
    // Description:
    //     Calculates max value for every metric, specified by MaxValueEquation.
    //     If the equation isn't present, current normalized metric value is used as max value.
    //
    // Input:
    //     TTypedValue_1_0* deltaMetricValues - (IN) previously read metric delta values
    //     TTypedValue_1_0* outMetricValues   - (IN) normalized metric values
    //     TTypedValue_1_0* outMaxValues      - (OUT) output max values
    //     CMetricSet&      metricSet         - MetricSet for calculations
    //
    //////////////////////////////////////////////////////////////////////////////
    void CMetricsCalculator::CalculateMaxValues( TTypedValue_1_0* deltaMetricValues, TTypedValue_1_0* outMetricValues, TTypedValue_1_0* outMaxValues, CMetricSet& metricSet )
    {
        if( !deltaMetricValues || !outMetricValues || !outMaxValues )
        {
            const uint32_t adapterId = OBTAIN_ADAPTER_ID( m_device );

            MD_ASSERT_A( adapterId, deltaMetricValues != nullptr );
            MD_ASSERT_A( adapterId, outMetricValues != nullptr );
            MD_ASSERT_A( adapterId, outMaxValues != nullptr );
            MD_LOG_A( adapterId, LOG_ERROR, "error: nullptr params" );
            return;
        }

        uint32_t metricsCount = metricSet.GetParams()->MetricsCount;

        for( uint32_t i = 0; i < metricsCount; i++ )
        {
            CMetric*           metric           = static_cast<CMetric*>( metricSet.GetMetric( i ) );
            TMetricParams_1_0* metricParams     = metric->GetParams();
            IEquation_1_0*     maxValueEquation = metricParams->MaxValueEquation;

            if( maxValueEquation )
            {
                // Do final calculation, may refer to global symbols, local delta results and local normalization results.
                // Normalization equation function is used because NormalizationEquation has the same restrictions as MaxValueEquation.
                CEquation& equationInternal = static_cast<CEquation&>( *maxValueEquation );
                outMaxValues[i]             = CalculateLocalNormalizationEquation( equationInternal, deltaMetricValues, outMetricValues, i );
            }
            else
            {
                outMaxValues[i] = outMetricValues[i];
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     CalculateReadEquation
    //
    // Description:
    //     Calculates the given read equation.
    //
    // Input:
    //     IEquation_1_0* equation  - read equation to calculate
    //     const uint8_t* rawReport - (IN) single raw report
    //
    // Output:
    //     TTypedValue_1_0 - output read value
    //
    //////////////////////////////////////////////////////////////////////////////
    TTypedValue_1_0 CMetricsCalculator::CalculateReadEquation( CEquation& equation, const uint8_t* rawReport )
    {
        TTypedValue_1_0 typedValue;
        const uint32_t  adapterId      = m_device->GetAdapter().GetAdapterId();
        bool            isValid        = true;
        uint32_t        algorithmCheck = 0;

        // Prepare the stack for calculations.
        std::stack<TTypedValue_1_0> equationStack = std::stack<TTypedValue_1_0>();

        for( uint32_t i = 0; i < equation.GetEquationElementsCount() && isValid; i++ )
        {
            TEquationElement_1_0* element = equation.GetEquationElement( i );
            switch( element->Type )
            {
                case EQUATION_ELEM_RD_BITFIELD:
                    typedValue.ValueUInt64 = ReadBitfield( (const uint8_t*) ( rawReport + element->ReadParams.ByteOffset ), element->ReadParams.BitOffset, element->ReadParams.BitsCount );
                    typedValue.ValueType   = VALUE_TYPE_UINT64;
                    isValid                = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_RD_UINT8:
                {
                    uint8_t byteValue      = *( (const uint8_t*) ( rawReport + element->ReadParams.ByteOffset ) );
                    typedValue.ValueUInt64 = (uint64_t) byteValue;
                    typedValue.ValueType   = VALUE_TYPE_UINT64;
                    isValid                = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;
                }

                case EQUATION_ELEM_RD_UINT16:
                {
                    unsigned short shortValue = *( (const unsigned short*) ( rawReport + element->ReadParams.ByteOffset ) );
                    typedValue.ValueUInt64    = (uint64_t) shortValue;
                    typedValue.ValueType      = VALUE_TYPE_UINT64;
                    isValid                   = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;
                }

                case EQUATION_ELEM_RD_UINT32:
                {
                    uint32_t dwordValue    = *( (const uint32_t*) ( rawReport + element->ReadParams.ByteOffset ) );
                    typedValue.ValueUInt64 = (uint64_t) dwordValue;
                    typedValue.ValueType   = VALUE_TYPE_UINT64;
                    isValid                = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;
                }

                case EQUATION_ELEM_RD_UINT64:
                    typedValue.ValueUInt64 = *( (const uint64_t*) ( rawReport + element->ReadParams.ByteOffset ) );
                    typedValue.ValueType   = VALUE_TYPE_UINT64;
                    isValid                = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_RD_FLOAT:
                    typedValue.ValueFloat = *( (const float*) ( rawReport + element->ReadParams.ByteOffset ) );
                    typedValue.ValueType  = VALUE_TYPE_FLOAT;
                    isValid               = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_RD_40BIT_CNTR:
                {
                    TLargeInteger largeValue;
                    largeValue.u.LowPart   = *( (const uint32_t*) ( rawReport + element->ReadParams.ByteOffset ) );
                    largeValue.u.HighPart  = ( uint32_t ) * ( (const uint8_t*) ( rawReport + element->ReadParams.ByteOffsetExt ) );
                    typedValue.ValueUInt64 = largeValue.QuadPart;
                    typedValue.ValueType   = VALUE_TYPE_UINT64;
                    isValid                = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;
                }

                case EQUATION_ELEM_IMM_UINT64:
                    typedValue.ValueUInt64 = element->ImmediateUInt64;
                    typedValue.ValueType   = VALUE_TYPE_UINT64;
                    isValid                = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_IMM_FLOAT:
                    typedValue.ValueFloat = element->ImmediateFloat;
                    typedValue.ValueType  = VALUE_TYPE_FLOAT;
                    isValid               = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_GLOBAL_SYMBOL:
                {
                    TTypedValue_1_0* pValue = GetGlobalSymbolValue( element->SymbolName );
                    if( pValue )
                    {
                        typedValue = *pValue;
                    }
                    else
                    {
                        typedValue.ValueUInt64 = 0;
                        typedValue.ValueType   = VALUE_TYPE_UINT64;
                    }
                    isValid = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;
                }

                case EQUATION_ELEM_INFORMATION_SYMBOL:
                {
                    if( strcmp( element->SymbolName, "PreviousContextId" ) == 0 )
                    {
                        // Return cached context ID from the previous report
                        typedValue.ValueUInt64 = m_contextIdPrev;
                    }
                    else
                    {
                        // TODO: not supported yet
                        typedValue.ValueUInt64 = 0;
                        MD_ASSERT_A( adapterId, false );
                    }
                    typedValue.ValueType = VALUE_TYPE_UINT64;
                    isValid              = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;
                }

                case EQUATION_ELEM_OPERATION:
                {
                    // Pop two values from stack
                    TTypedValue_1_0 valueLast = equationStack.top();
                    equationStack.pop();
                    algorithmCheck--;
                    TTypedValue_1_0 valuePrev = equationStack.top();
                    equationStack.pop();
                    algorithmCheck--;

                    typedValue = CalculateEquationElemOperation( element->Operation, valuePrev, valueLast );
                    isValid    = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;
                }

                default:
                    MD_ASSERT_A( adapterId, false );
                    break;
            }
        }
        // Here should be only 1 element on the list - the result (if the equation is fine)
        MD_ASSERT_A( adapterId, algorithmCheck == 1 );

        if( isValid && algorithmCheck == 1 )
        {
            typedValue = equationStack.top();
            equationStack.pop();
        }
        else
        {
            typedValue.ValueUInt64 = 0;
            typedValue.ValueType   = VALUE_TYPE_UINT64;
        }

        return typedValue;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     CalculateReadEquationAndDelta
    //
    // Description:
    //     Calculates the given read equation using delta function directly after reading
    //     raw offsets.
    //
    // Input:
    //     IEquation_1_0*     equation       - read equation to calculate
    //     TDeltaFunction_1_0 deltaFunction  - delta function to use during calculations
    //     const uint8_t*     pRawReportLast - (IN) last (next) single raw report
    //     const uint8_t*     pRawReportPrev - (IN) previous single raw report
    //
    // Output:
    //     TTypedValue_1_0 - output read value
    //
    //////////////////////////////////////////////////////////////////////////////
    TTypedValue_1_0 CMetricsCalculator::CalculateReadEquationAndDelta( CEquation& equation, TDeltaFunction_1_0 deltaFunction, const uint8_t* pRawReportLast, const uint8_t* pRawReportPrev )
    {
        TTypedValue_1_0    typedValue, typedValuePrev, typedValueLast;
        TDeltaFunction_1_0 readDeltaFunction;
        // As we calculate delta when reading operands DELTA_NS_TIME works as a normal DELTA_32
        if( deltaFunction.FunctionType == DELTA_NS_TIME )
        {
            readDeltaFunction.FunctionType = DELTA_N_BITS;
            readDeltaFunction.BitsCount    = 32;
        }
        else
        {
            readDeltaFunction = deltaFunction;
        }

        const uint32_t adapterId      = m_device->GetAdapter().GetAdapterId();
        bool           isValid        = true;
        uint32_t       algorithmCheck = 0;

        // Prepare the stack for calculations.
        std::stack<TTypedValue_1_0> equationStack = std::stack<TTypedValue_1_0>();

        for( uint32_t i = 0; i < equation.GetEquationElementsCount() && isValid; i++ )
        {
            TEquationElement_1_0* element = equation.GetEquationElement( i );
            switch( element->Type )
            {
                case EQUATION_ELEM_RD_BITFIELD:

                    typedValuePrev.ValueUInt64 = ReadBitfield( pRawReportPrev + element->ReadParams.ByteOffset, element->ReadParams.BitOffset, element->ReadParams.BitsCount );
                    typedValuePrev.ValueType   = VALUE_TYPE_UINT64;

                    typedValueLast.ValueUInt64 = ReadBitfield( pRawReportLast + element->ReadParams.ByteOffset, element->ReadParams.BitOffset, element->ReadParams.BitsCount );
                    typedValueLast.ValueType   = VALUE_TYPE_UINT64;

                    typedValue = CalculateDeltaFunction( readDeltaFunction, typedValueLast, typedValuePrev );
                    isValid    = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_RD_UINT8:
                    typedValuePrev.ValueUInt64 =
                        ( uint64_t ) * ( (const uint8_t*) ( pRawReportPrev + element->ReadParams.ByteOffset ) );
                    typedValuePrev.ValueType = VALUE_TYPE_UINT64;

                    typedValueLast.ValueUInt64 =
                        ( uint64_t ) * ( (const uint8_t*) ( pRawReportLast + element->ReadParams.ByteOffset ) );
                    typedValueLast.ValueType = VALUE_TYPE_UINT64;

                    typedValue = CalculateDeltaFunction( readDeltaFunction, typedValueLast, typedValuePrev );
                    isValid    = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_RD_UINT16:
                    typedValuePrev.ValueUInt64 =
                        ( uint64_t ) * ( (const unsigned short*) ( pRawReportPrev + element->ReadParams.ByteOffset ) );
                    typedValuePrev.ValueType = VALUE_TYPE_UINT64;

                    typedValueLast.ValueUInt64 =
                        ( uint64_t ) * ( (const unsigned short*) ( pRawReportLast + element->ReadParams.ByteOffset ) );
                    typedValueLast.ValueType = VALUE_TYPE_UINT64;

                    typedValue = CalculateDeltaFunction( readDeltaFunction, typedValueLast, typedValuePrev );
                    isValid    = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_RD_UINT32:
                    typedValuePrev.ValueUInt64 =
                        ( uint64_t ) * ( (const uint32_t*) ( pRawReportPrev + element->ReadParams.ByteOffset ) );
                    typedValuePrev.ValueType = VALUE_TYPE_UINT64;

                    typedValueLast.ValueUInt64 =
                        ( uint64_t ) * ( (const uint32_t*) ( pRawReportLast + element->ReadParams.ByteOffset ) );
                    typedValueLast.ValueType = VALUE_TYPE_UINT64;

                    typedValue = CalculateDeltaFunction( readDeltaFunction, typedValueLast, typedValuePrev );
                    isValid    = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_RD_UINT64:
                    typedValuePrev.ValueUInt64 = *( (const uint64_t*) ( pRawReportPrev + element->ReadParams.ByteOffset ) );
                    typedValuePrev.ValueType   = VALUE_TYPE_UINT64;

                    typedValueLast.ValueUInt64 = *( (const uint64_t*) ( pRawReportLast + element->ReadParams.ByteOffset ) );
                    typedValueLast.ValueType   = VALUE_TYPE_UINT64;

                    typedValue = CalculateDeltaFunction( readDeltaFunction, typedValueLast, typedValuePrev );
                    isValid    = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_RD_FLOAT:
                    typedValuePrev.ValueFloat = *( (const float*) ( pRawReportPrev + element->ReadParams.ByteOffset ) );
                    typedValuePrev.ValueType  = VALUE_TYPE_FLOAT;

                    typedValueLast.ValueFloat = *( (const float*) ( pRawReportLast + element->ReadParams.ByteOffset ) );
                    typedValueLast.ValueType  = VALUE_TYPE_FLOAT;

                    typedValue = CalculateDeltaFunction( readDeltaFunction, typedValueLast, typedValuePrev );
                    isValid    = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_RD_40BIT_CNTR:
                {
                    TLargeInteger largeValue;
                    largeValue.u.LowPart       = *( (const uint32_t*) ( pRawReportPrev + element->ReadParams.ByteOffset ) );
                    largeValue.u.HighPart      = ( uint32_t ) * ( (const uint8_t*) ( pRawReportPrev + element->ReadParams.ByteOffsetExt ) );
                    typedValuePrev.ValueUInt64 = largeValue.QuadPart;
                    typedValuePrev.ValueType   = VALUE_TYPE_UINT64;

                    largeValue.u.LowPart       = *( (const uint32_t*) ( pRawReportLast + element->ReadParams.ByteOffset ) );
                    largeValue.u.HighPart      = ( uint32_t ) * ( (const uint8_t*) ( pRawReportLast + element->ReadParams.ByteOffsetExt ) );
                    typedValueLast.ValueUInt64 = largeValue.QuadPart;
                    typedValueLast.ValueType   = VALUE_TYPE_UINT64;

                    typedValue = CalculateDeltaFunction( readDeltaFunction, typedValueLast, typedValuePrev );
                    isValid    = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;
                }

                case EQUATION_ELEM_IMM_UINT64:
                    typedValue.ValueUInt64 = element->ImmediateUInt64;
                    typedValue.ValueType   = VALUE_TYPE_UINT64;
                    isValid                = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_IMM_FLOAT:
                    typedValue.ValueFloat = element->ImmediateFloat;
                    typedValue.ValueType  = VALUE_TYPE_FLOAT;
                    isValid               = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_GLOBAL_SYMBOL:
                {
                    TTypedValue_1_0* pValue = GetGlobalSymbolValue( element->SymbolName );
                    if( pValue )
                    {
                        typedValue = *pValue;
                    }
                    else
                    {
                        typedValue.ValueUInt64 = 0LL;
                        typedValue.ValueType   = VALUE_TYPE_UINT64;
                    }
                    isValid = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;
                }

                case EQUATION_ELEM_OPERATION:
                {
                    // Pop two values from stack
                    TTypedValue_1_0 valueLast = equationStack.top();
                    equationStack.pop();
                    algorithmCheck--;
                    TTypedValue_1_0 valuePrev = equationStack.top();
                    equationStack.pop();
                    algorithmCheck--;

                    typedValue = CalculateEquationElemOperation( element->Operation, valuePrev, valueLast );
                    isValid    = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;
                }

                default:
                    MD_ASSERT_A( adapterId, false );
                    break;
            }
        }
        // here should be only 1 element on the list - the result (if the equation is fine)
        MD_ASSERT_A( adapterId, algorithmCheck == 1 );

        if( isValid && algorithmCheck == 1 )
        {
            typedValue = equationStack.top();
            equationStack.pop();
        }
        else
        {
            typedValue.ValueUInt64 = 0LL;
            typedValue.ValueType   = VALUE_TYPE_UINT64;
        }

        return typedValue;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     CalculateDeltaFunction
    //
    // Description:
    //     Calculates the given delta function.
    //
    // Input:
    //     TDeltaFunction_1_0     deltaFunction  - delta function to be calculated
    //     const TTypedValue_1_0& lastValue     - (IN) last (next) value
    //     const TTypedValue_1_0& previousValue - (IN) previous value
    //
    // Output:
    //     TTypedValue_1_0 - output calculated delta value
    //
    //////////////////////////////////////////////////////////////////////////////
    TTypedValue_1_0 CMetricsCalculator::CalculateDeltaFunction( TDeltaFunction_1_0 deltaFunction, const TTypedValue_1_0& lastValue, const TTypedValue_1_0& previousValue )
    {
        TTypedValue_1_0 typedValue;

        switch( deltaFunction.FunctionType )
        {
            case DELTA_BOOL_OR:
                typedValue.ValueUInt64 = ( ( lastValue.ValueUInt64 | previousValue.ValueUInt64 ) != 0 ) ? 1LL : 0LL;
                typedValue.ValueType   = VALUE_TYPE_UINT64;
                return typedValue;

            case DELTA_BOOL_XOR:
                typedValue.ValueUInt64 = ( ( lastValue.ValueUInt64 ^ previousValue.ValueUInt64 ) != 0 ) ? 1LL : 0LL;
                typedValue.ValueType   = VALUE_TYPE_UINT64;
                return typedValue;

            case DELTA_GET_LAST:
                return lastValue;

            case DELTA_GET_PREVIOUS:
                return previousValue;

            case DELTA_NS_TIME:
                // No 'break' intentional - NS_TIME should be used only for overflow functions, here use as DELTA 32
                deltaFunction.BitsCount = 32;

            case DELTA_N_BITS:
                if( deltaFunction.BitsCount <= 64 )
                {
                    if( previousValue.ValueUInt64 > lastValue.ValueUInt64 )
                    {
                        uint64_t value         = lastValue.ValueUInt64 | ( 1LL << deltaFunction.BitsCount );
                        typedValue.ValueUInt64 = value - previousValue.ValueUInt64;
                        typedValue.ValueType   = VALUE_TYPE_UINT64;
                        return typedValue;
                    }
                    else
                    {
                        typedValue.ValueUInt64 = lastValue.ValueUInt64 - previousValue.ValueUInt64;
                        typedValue.ValueType   = VALUE_TYPE_UINT64;
                        return typedValue;
                    }
                }
                break;

            case DELTA_FUNCTION_NULL:
                typedValue.ValueUInt64 = 0;
                typedValue.ValueType   = VALUE_TYPE_UINT64;
                return typedValue;

            default:
                MD_ASSERT_A( m_device->GetAdapter().GetAdapterId(), false );
                break;
        }

        typedValue.ValueUInt64 = 0;
        typedValue.ValueType   = VALUE_TYPE_UINT64;

        return typedValue;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     CalculateLocalNormalizationEquation
    //
    // Description:
    //     Calculates the given normalization equation.
    //
    // Input:
    //     IEquation_1_0*   equation         - (IN) normalization equation to be calculated
    //     TTypedValue_1_0* deltaValues      - (IN) previously calculated / read delta values
    //     TTypedValue_1_0* outValues        - (IN) so far normalized values (metrics with lower indices)
    //     CMetricSet*      metricSet        - MetricSet for calculation
    //     uint32_t         currentMetricIdx - index of the currently calculated metric
    //
    // Output:
    //     TTypedValue_1_0 - output normalized value
    //
    //////////////////////////////////////////////////////////////////////////////
    TTypedValue_1_0 CMetricsCalculator::CalculateLocalNormalizationEquation( CEquation& equation, TTypedValue_1_0* deltaValues, TTypedValue_1_0* outValues, uint32_t currentMetricIdx )
    {
        TTypedValue_1_0 typedValue;
        bool            isValid        = true;
        uint32_t        algorithmCheck = 0;
        const uint32_t  adapterId      = m_device->GetAdapter().GetAdapterId();

        // Prepare the stack for calculations.
        std::stack<TTypedValue_1_0> equationStack = std::stack<TTypedValue_1_0>();

        for( uint32_t i = 0; i < equation.GetEquationElementsCount() && isValid; i++ )
        {
            TEquationElement_1_0* element = equation.GetEquationElement( i );
            switch( element->Type )
            {
                case EQUATION_ELEM_RD_BITFIELD:
                case EQUATION_ELEM_RD_UINT8:
                case EQUATION_ELEM_RD_UINT16:
                case EQUATION_ELEM_RD_UINT32:
                case EQUATION_ELEM_RD_UINT64:
                case EQUATION_ELEM_RD_FLOAT:
                case EQUATION_ELEM_RD_40BIT_CNTR:
                    // Not allowed in norm equation
                    break;

                case EQUATION_ELEM_IMM_FLOAT:
                    typedValue.ValueFloat = element->ImmediateFloat;
                    typedValue.ValueType  = VALUE_TYPE_FLOAT;
                    isValid               = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_IMM_UINT64:
                    typedValue.ValueUInt64 = element->ImmediateUInt64;
                    typedValue.ValueType   = VALUE_TYPE_UINT64;
                    isValid                = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_SELF_COUNTER_VALUE:
                    // Get result of delta equation
                    typedValue = deltaValues[currentMetricIdx];
                    isValid    = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;

                case EQUATION_ELEM_LOCAL_COUNTER_SYMBOL:
                {
                    // Cast to get access to MetricsIndexInternal variable
                    CEquationElementInternal* internalElement = (CEquationElementInternal*) element;

                    // The index is higher than or equals 0 if the symbol name was found, otherwise it equals -1
                    if( internalElement->MetricIndexInternal >= 0 )
                    {
                        typedValue = deltaValues[internalElement->MetricIndexInternal];
                    }
                    else
                    {
                        typedValue.ValueUInt64 = 0;
                        typedValue.ValueType   = VALUE_TYPE_UINT64;
                    }

                    isValid = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;
                }

                case EQUATION_ELEM_LOCAL_METRIC_SYMBOL:
                {
                    // Cast to get access to MetricsIndexInternal variable
                    CEquationElementInternal* internalElement = (CEquationElementInternal*) element;

                    // The index is higher than or equals 0 if the symbol name was found, otherwise it equals -1
                    if( internalElement->MetricIndexInternal >= 0 )
                    {
                        typedValue = outValues[internalElement->MetricIndexInternal];
                    }
                    else
                    {
                        typedValue.ValueUInt64 = 0;
                        typedValue.ValueType   = VALUE_TYPE_UINT64;
                    }

                    isValid = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;
                }

                case EQUATION_ELEM_GLOBAL_SYMBOL:
                {
                    TTypedValue_1_0* pValue = GetGlobalSymbolValue( element->SymbolName );
                    if( pValue )
                    {
                        typedValue = *pValue;
                    }
                    else
                    {
                        typedValue.ValueUInt64 = 0;
                        typedValue.ValueType   = VALUE_TYPE_UINT64;
                    }
                    isValid = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;
                }

                case EQUATION_ELEM_OPERATION:
                {
                    // pop two values from stack
                    TTypedValue_1_0 valueLast = equationStack.top();
                    equationStack.pop();

                    algorithmCheck--;
                    TTypedValue_1_0 valuePrev = equationStack.top();
                    equationStack.pop();
                    algorithmCheck--;

                    typedValue = CalculateEquationElemOperation( element->Operation, valuePrev, valueLast );
                    isValid    = EquationStackPush( equationStack, typedValue, algorithmCheck );
                    break;
                }

                case EQUATION_ELEM_STD_NORM_GPU_DURATION:
                    // equation stack should be empty
                    MD_ASSERT_A( adapterId, algorithmCheck == 0 );

                    // compute $Self $gpuCoreClocks FDIV 100 FMUL
                    if( m_gpuCoreClocks != 0 )
                    {
                        float self          = CastToFloat( deltaValues[currentMetricIdx] );
                        float gpuCoreClocks = (float) m_gpuCoreClocks;

                        typedValue.ValueFloat = 100.0f * self / gpuCoreClocks;
                        typedValue.ValueType  = VALUE_TYPE_FLOAT;
                        return typedValue;
                    }
                    else
                    {
                        // Warning: GpuCoreClocks is 0
                        typedValue.ValueFloat = 0.0f;
                        typedValue.ValueType  = VALUE_TYPE_FLOAT;
                        return typedValue;
                    }

                case EQUATION_ELEM_STD_NORM_EU_AGGR_DURATION:
                    // equation stack should be empty
                    MD_ASSERT_A( adapterId, algorithmCheck == 0 );
                    // m_euCoresCount is needed here
                    MD_ASSERT_A( adapterId, m_euCoresCount != 0 );

                    // compute $Self $gpuCoreClocks $EUsCount UMUL FDIV 100 FMUL
                    if( m_gpuCoreClocks != 0 && m_euCoresCount != 0 )
                    {
                        float self          = CastToFloat( deltaValues[currentMetricIdx] );
                        float gpuCoreClocks = (float) ( m_gpuCoreClocks * m_euCoresCount );

                        typedValue.ValueFloat = 100.0f * self / gpuCoreClocks;
                        typedValue.ValueType  = VALUE_TYPE_FLOAT;
                        return typedValue;
                    }
                    else
                    {
                        // Warning: GpuCoreClocks or euCoresCount is 0
                        typedValue.ValueFloat = 0.0f;
                        typedValue.ValueType  = VALUE_TYPE_FLOAT;
                        return typedValue;
                    }

                default:
                    break;
            }
        }
        // here should be only 1 element on the list - the result (if the equation is fine)
        MD_ASSERT_A( adapterId, algorithmCheck == 1 );

        if( isValid && algorithmCheck == 1 )
        {
            typedValue = equationStack.top();
            equationStack.pop();
        }
        else
        {
            typedValue.ValueUInt64 = 0;
            typedValue.ValueType   = VALUE_TYPE_UINT64;
        }

        return typedValue;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     CalculateEquationElemOperation
    //
    // Description:
    //     Calculates the given equation operation.
    //
    // Input:
    //     TEquationOperation     operation - operation to be calculated
    //     const TTypedValue_1_0& valuePrev - (IN) previous value
    //     const TTypedValue_1_0& valueLast - (IN) last (next) value
    //
    // Output:
    //     TTypedValue_1_0 - output calculated value
    //
    //////////////////////////////////////////////////////////////////////////////
    TTypedValue_1_0 CMetricsCalculator::CalculateEquationElemOperation( TEquationOperation operation, const TTypedValue_1_0& valuePrev, const TTypedValue_1_0& valueLast )
    {
        TTypedValue_1_0 value;
        value.ValueUInt64 = 0;
        value.ValueType   = VALUE_TYPE_UINT64;

        switch( operation )
        {
            case EQUATION_OPER_AND:
                value.ValueUInt64 = CastToUInt64( valuePrev ) & CastToUInt64( valueLast );
                value.ValueType   = VALUE_TYPE_UINT64;
                break;

            case EQUATION_OPER_OR:
                value.ValueUInt64 = CastToUInt64( valuePrev ) | CastToUInt64( valueLast );
                value.ValueType   = VALUE_TYPE_UINT64;
                break;

            case EQUATION_OPER_RSHIFT:
                value.ValueUInt64 = CastToUInt64( valuePrev ) >> CastToUInt64( valueLast );
                value.ValueType   = VALUE_TYPE_UINT64;
                break;

            case EQUATION_OPER_LSHIFT:
                value.ValueUInt64 = CastToUInt64( valuePrev ) << CastToUInt64( valueLast );
                value.ValueType   = VALUE_TYPE_UINT64;
                break;

            case EQUATION_OPER_XOR:
                value.ValueUInt64 = CastToUInt64( valuePrev ) ^ CastToUInt64( valueLast );
                value.ValueType   = VALUE_TYPE_UINT64;
                break;

            case EQUATION_OPER_XNOR:
                value.ValueUInt64 = ~( CastToUInt64( valuePrev ) ^ CastToUInt64( valueLast ) );
                value.ValueType   = VALUE_TYPE_UINT64;
                break;

            case EQUATION_OPER_AND_L:
                value.ValueBool = CastToUInt64( valuePrev ) && CastToUInt64( valueLast );
                value.ValueType = VALUE_TYPE_BOOL;
                break;

            case EQUATION_OPER_EQUALS:
                value.ValueBool = CastToUInt64( valuePrev ) == CastToUInt64( valueLast );
                value.ValueType = VALUE_TYPE_BOOL;
                break;

            case EQUATION_OPER_UADD:
                value.ValueUInt64 = CastToUInt64( valuePrev ) + CastToUInt64( valueLast );
                value.ValueType   = VALUE_TYPE_UINT64;
                break;

            case EQUATION_OPER_USUB:
                value.ValueUInt64 = CastToUInt64( valuePrev ) - CastToUInt64( valueLast );
                value.ValueType   = VALUE_TYPE_UINT64;
                break;

            case EQUATION_OPER_UDIV:
                if( CastToUInt64( valueLast ) != 0LL )
                {
                    value.ValueUInt64 = CastToUInt64( valuePrev ) / CastToUInt64( valueLast );
                    value.ValueType   = VALUE_TYPE_UINT64;
                }
                else
                {
                    value.ValueUInt64 = 0ULL;
                    value.ValueType   = VALUE_TYPE_UINT64;
                }
                break;

            case EQUATION_OPER_UMUL:
                value.ValueUInt64 = CastToUInt64( valuePrev ) * CastToUInt64( valueLast );
                value.ValueType   = VALUE_TYPE_UINT64;
                break;

            case EQUATION_OPER_FADD:
                value.ValueFloat = CastToFloat( valuePrev ) + CastToFloat( valueLast );
                value.ValueType  = VALUE_TYPE_FLOAT;
                break;

            case EQUATION_OPER_FSUB:
                value.ValueFloat = CastToFloat( valuePrev ) - CastToFloat( valueLast );
                value.ValueType  = VALUE_TYPE_FLOAT;
                break;

            case EQUATION_OPER_FMUL:
                value.ValueFloat = CastToFloat( valuePrev ) * CastToFloat( valueLast );
                value.ValueType  = VALUE_TYPE_FLOAT;
                break;

            case EQUATION_OPER_FDIV:
                if( CastToFloat( valueLast ) != 0LL )
                {
                    value.ValueFloat = CastToFloat( valuePrev ) / CastToFloat( valueLast );
                    value.ValueType  = VALUE_TYPE_FLOAT;
                }
                else
                {
                    value.ValueFloat = 0.0f;
                    value.ValueType  = VALUE_TYPE_FLOAT;
                }
                break;

            case EQUATION_OPER_UGT:
                value.ValueBool = CastToUInt64( valuePrev ) > CastToUInt64( valueLast );
                value.ValueType = VALUE_TYPE_BOOL;
                break;

            case EQUATION_OPER_ULT:
                value.ValueBool = CastToUInt64( valuePrev ) < CastToUInt64( valueLast );
                value.ValueType = VALUE_TYPE_BOOL;
                break;

            case EQUATION_OPER_UGTE:
                value.ValueBool = CastToUInt64( valuePrev ) >= CastToUInt64( valueLast );
                value.ValueType = VALUE_TYPE_BOOL;
                break;

            case EQUATION_OPER_ULTE:
                value.ValueBool = CastToUInt64( valuePrev ) <= CastToUInt64( valueLast );
                value.ValueType = VALUE_TYPE_BOOL;
                break;

            case EQUATION_OPER_FGT:
                value.ValueBool = CastToFloat( valuePrev ) > CastToFloat( valueLast );
                value.ValueType = VALUE_TYPE_BOOL;
                break;

            case EQUATION_OPER_FLT:
                value.ValueBool = CastToFloat( valuePrev ) < CastToFloat( valueLast );
                value.ValueType = VALUE_TYPE_BOOL;
                break;

            case EQUATION_OPER_FGTE:
                value.ValueBool = CastToFloat( valuePrev ) >= CastToFloat( valueLast );
                value.ValueType = VALUE_TYPE_BOOL;
                break;

            case EQUATION_OPER_FLTE:
                value.ValueBool = CastToFloat( valuePrev ) <= CastToFloat( valueLast );
                value.ValueType = VALUE_TYPE_BOOL;
                break;

            case EQUATION_OPER_UMIN:
                // (std::min) - braces to bypass windows.h min/max errors
                value.ValueUInt64 = ( std::min )( CastToUInt64( valuePrev ), CastToUInt64( valueLast ) );
                value.ValueType   = VALUE_TYPE_UINT64;
                break;

            case EQUATION_OPER_UMAX:
                // (std::min) - braces to bypass windows.h min/max errors
                value.ValueUInt64 = ( std::max )( CastToUInt64( valuePrev ), CastToUInt64( valueLast ) );
                value.ValueType   = VALUE_TYPE_UINT64;
                break;

            case EQUATION_OPER_FMIN:
                // (std::min) - braces to bypass windows.h min/max errors
                value.ValueFloat = ( std::min )( CastToFloat( valuePrev ), CastToFloat( valueLast ) );
                value.ValueType  = VALUE_TYPE_FLOAT;
                break;

            case EQUATION_OPER_FMAX:
                // (std::min) - braces to bypass windows.h min/max errors
                value.ValueFloat = ( std::max )( CastToFloat( valuePrev ), CastToFloat( valueLast ) );
                value.ValueType  = VALUE_TYPE_FLOAT;
                break;

            default:
                value.ValueUInt64 = 0;
                value.ValueType   = VALUE_TYPE_UINT64;
                break;
        }

        return value;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     ReadSingleInformation
    //
    // Description:
    //     Reads single information.
    //
    // Input:
    //     const uint8_t*    rawReport   - single raw report
    //     IInformation_1_0* information - information to calculate
    //     uint32_t          apiMask     - API mask (needed for choosing proper equation)
    //     TTypedValue_1_0*  outValue    - (OUT) read information value
    //
    //////////////////////////////////////////////////////////////////////////////
    void CMetricsCalculator::ReadSingleInformation( const uint8_t* rawReport, IInformation_1_0* information, uint32_t apiMask, TTypedValue_1_0* outValue )
    {
        if( !rawReport || !information || !outValue )
        {
            const uint32_t adapterId = OBTAIN_ADAPTER_ID( m_device );

            MD_ASSERT_A( adapterId, rawReport != nullptr );
            MD_ASSERT_A( adapterId, information != nullptr );
            MD_ASSERT_A( adapterId, outValue != nullptr );
            MD_LOG_A( adapterId, LOG_ERROR, "error: nullptr params" );
            return;
        }

        uint32_t                streamMask        = API_TYPE_IOSTREAM | API_TYPE_BBSTREAM;
        TInformationParams_1_0* informationParams = information->GetParams();
        IEquation_1_0*          equation          = ( apiMask & streamMask ) ? informationParams->IoReadEquation : informationParams->QueryReadEquation;

        if( equation )
        {
            CEquation& equationInternal = static_cast<CEquation&>( *equation );
            *outValue                   = CalculateReadEquation( equationInternal, rawReport );
        }
        else
        {
            outValue->ValueUInt64 = 0ULL;
        }

        if( informationParams->InfoType == INFORMATION_TYPE_FLAG )
        {
            outValue->ValueType = VALUE_TYPE_BOOL;
        }
        else
        {
            outValue->ValueType = VALUE_TYPE_UINT64;
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     CastToUInt32
    //
    // Description:
    //     Casts data to uint32.
    //
    // Input:
    //     const TTypedValue_1_0& value - typed value to cast
    //
    // Output:
    //     uint32_t - cast uint32 value
    //
    //////////////////////////////////////////////////////////////////////////////
    uint32_t CMetricsCalculator::CastToUInt32( const TTypedValue_1_0& value )
    {
        switch( value.ValueType )
        {
            case VALUE_TYPE_BOOL:
                return ( value.ValueBool ) ? 1U : 0U;

            case VALUE_TYPE_UINT32:
                return value.ValueUInt32;

            case VALUE_TYPE_UINT64:
                return static_cast<uint32_t>( value.ValueUInt64 );

            case VALUE_TYPE_FLOAT:
                return static_cast<uint32_t>( value.ValueFloat );

            default:
                return 0U;
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     CastToUInt64
    //
    // Description:
    //     Casts data to uint64.
    //
    // Input:
    //     const TTypedValue_1_0& value - typed value to cast
    //
    // Output:
    //     uint64_t - cast uint64 value
    //
    //////////////////////////////////////////////////////////////////////////////
    uint64_t CMetricsCalculator::CastToUInt64( const TTypedValue_1_0& value )
    {
        switch( value.ValueType )
        {
            case VALUE_TYPE_BOOL:
                return ( value.ValueBool ) ? 1LL : 0LL;

            case VALUE_TYPE_UINT32:
                return static_cast<uint64_t>( value.ValueUInt32 );

            case VALUE_TYPE_UINT64:
                return value.ValueUInt64;

            case VALUE_TYPE_FLOAT:
                return static_cast<uint64_t>( value.ValueFloat );

            default:
                return 0LL;
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     CastToFloat
    //
    // Description:
    //     Casts data to float.
    //
    // Input:
    //     const TTypedValue_1_0& value - typed value to cast
    //
    // Output:
    //     float - cast float value
    //
    //////////////////////////////////////////////////////////////////////////////
    float CMetricsCalculator::CastToFloat( const TTypedValue_1_0& value )
    {
        switch( value.ValueType )
        {
            case VALUE_TYPE_BOOL:
                return ( value.ValueBool ) ? 1.0f : 0.0f;

            case VALUE_TYPE_UINT32:
                return static_cast<float>( value.ValueUInt32 );

            case VALUE_TYPE_UINT64:
                return static_cast<float>( value.ValueUInt64 );

            case VALUE_TYPE_FLOAT:
                return value.ValueFloat;

            default:
                return 0.0f;
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     CastToBoolean
    //
    // Description:
    //     Casts data to boolean.
    //
    // Input:
    //     const TTypedValue_1_0& value - typed value to cast
    //
    // Output:
    //     bool - cast boolean value
    //
    //////////////////////////////////////////////////////////////////////////////
    bool CMetricsCalculator::CastToBoolean( const TTypedValue_1_0& value )
    {
        switch( value.ValueType )
        {
            case VALUE_TYPE_BOOL:
                return value.ValueBool;

            case VALUE_TYPE_UINT32:
                return value.ValueUInt32 != 0U;

            case VALUE_TYPE_UINT64:
                return value.ValueUInt64 != 0LL;

            case VALUE_TYPE_FLOAT:
                return value.ValueFloat != 0.0f;

            default:
                return false;
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     ReadBitfield
    //
    // Description:
    //     Reads specified bitfield from a report.
    //
    // Input:
    //     const uint8_t* rawReport - single raw report data
    //     uint32_t       bitOffset - field offset
    //     uint32_t       bitCount  - bit count to read
    //
    // Output:
    //     uint64_t - read value
    //
    //////////////////////////////////////////////////////////////////////////////
    uint64_t CMetricsCalculator::ReadBitfield( const uint8_t* rawReport, uint32_t bitOffset, uint32_t bitCount )
    {
        if( !rawReport || ( bitCount > 32 ) || ( bitCount == 0 ) || ( bitCount + bitOffset > 32 ) )
        {
            const uint32_t adapterId = OBTAIN_ADAPTER_ID( m_device );

            MD_ASSERT_A( adapterId, false );
            MD_LOG_A( adapterId, LOG_ERROR, "error: invalid params" );
            return 0;
        }

        // Build mask
        uint32_t mask = MD_BITMASK_RANGE( bitOffset, bitOffset + bitCount );

        // Get integer in the way in is alignment safe
        uint32_t data = ( *rawReport ) | ( ( *( rawReport + 1 ) ) << 8 ) | ( ( *( rawReport + 2 ) ) << 16 ) | ( ( *( rawReport + 3 ) ) << 24 );

        return (uint64_t) ( ( data & mask ) >> bitOffset );
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     GetMetricsDevice
    //
    // Description:
    //     Returns metrics device
    //
    // Output:
    //     CMetricsDevice* - metrics device
    //
    //////////////////////////////////////////////////////////////////////////////
    CMetricsDevice* CMetricsCalculator::GetMetricsDevice()
    {
        return m_device;
    }

    //////////////////////////////////////////////////////////////////////////////
    //
    // Class:
    //     CMetricsCalculator
    //
    // Method:
    //     GetGlobalSymbolValue
    //
    // Description:
    //     Returns global symbol of a given name. Uses MetricsDevice.
    //
    // Input:
    //     const char* symbolName - global symbol name
    //
    // Output:
    //     TTypedValue_1_0* - global symbol typed value, null if error
    //
    //////////////////////////////////////////////////////////////////////////////
    TTypedValue_1_0* CMetricsCalculator::GetGlobalSymbolValue( const char* symbolName )
    {
        const uint32_t adapterId = OBTAIN_ADAPTER_ID( m_device );

        MD_CHECK_PTR_RET_A( adapterId, m_device, nullptr );
        MD_CHECK_PTR_RET_A( adapterId, symbolName, nullptr );

        for( uint32_t i = 0; i < m_device->GetParams()->GlobalSymbolsCount; i++ )
        {
            TGlobalSymbol_1_0* symbol = m_device->GetGlobalSymbol( i );
            if( symbol && strcmp( symbol->SymbolName, symbolName ) == 0 )
            {
                return &( symbol->SymbolTypedValue );
            }
        }
        return nullptr;
    }

} // namespace MetricsDiscoveryInternal
