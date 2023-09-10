#include <ql/qldefines.hpp>
#if !defined(BOOST_ALL_NO_LIB) && defined(BOOST_MSVC)
#  include <ql/auto_link.hpp>
#endif

#include <ql/quantlib.hpp>

#include <iostream>
#include <iomanip>

using namespace QuantLib;

Size numRows = 6;
Size numCols = 6;

Integer swapLengths[] = {
      1,     2,     3,     4,     5,  7};

Volatility swaptionVols[] = {
    0.3556    ,0.3742,    0.3734,    0.3664,    0.3561,    0.3428,
    0.3936    ,0.3901,    0.3802,    0.3682,    0.3557,    0.3382,
    0.3834    ,0.3728,    0.3643,    0.3560,    0.3471,    0.3270,
    0.3643    ,0.3502,    0.3407,    0.3306,    0.3202,    0.3024,
    0.3378    ,0.3261,    0.3174,    0.3082,    0.2994,    0.2853,
    0.2863    ,0.2792,    0.2737,    0.2672,    0.2620,    0.2564
};

void calibrateModel(
          const ext::shared_ptr<ShortRateModel>& model,
          const std::vector<ext::shared_ptr<BlackCalibrationHelper>>& swaptions) {

    std::vector<ext::shared_ptr<CalibrationHelper>> helpers(swaptions.begin(), swaptions.end());
    LevenbergMarquardt om;
    model->calibrate(helpers, om,
                     EndCriteria(400, 100, 1.0e-8, 1.0e-8, 1.0e-8));

    // Output the implied Black volatilities
    for (Size i=0; i<numRows; i++) {
        Size j = numCols - i -1; // 1x5, 2x4, 3x3, 4x2, 5x1
        Size k = i*numCols + j;
        Real npv = swaptions[i]->modelValue();
        Volatility implied = swaptions[i]->impliedVolatility(npv, 1e-4,
                                                             1000, 0.05, 0.50);
        Volatility diff = implied - swaptionVols[k];

        std::cout << i+1 << "x" << swapLengths[j]
                  << std::setprecision(5) << std::noshowpos
                  << ": model " << std::setw(7) << io::volatility(implied)
                  << ", market " << std::setw(7)
                  << io::volatility(swaptionVols[k])
                  << " (" << std::setw(7) << std::showpos
                  << io::volatility(diff) << std::noshowpos << ")\n";
    }
}

int main(int, char* []) {

    try {

        std::cout << std::endl;

        Date todaysDate(30, August, 2023);
        Calendar calendar = TARGET(); // United States!!
        Date settlementDate(31, August, 2023);
        Settings::instance().evaluationDate() = todaysDate;
        
        DayCounter termStructureDayCounter = Actual360();
        std::vector<ext::shared_ptr<RateHelper>> sofrInstruments;
        

        double sofr_3m = 0.05417;
        double sofr_6m = 0.05494;
        double sofr_1y = 0.05480;
        double sofr_2y = 0.04949;
        double sofr_3y = 0.04598;
        double sofr_4y = 0.04371;
        double sofr_5y = 0.04231;
        double sofr_7y = 0.04068;
        
        std::map<Period, ext::shared_ptr<Quote>> longOisQuotes =
        {
            {3 * Months, ext::make_shared<SimpleQuote>(0.05417)},
            {6 * Months, ext::make_shared<SimpleQuote>(0.05494)},
            {12 * Months,ext::make_shared<SimpleQuote>(0.0548)},
            {2 * Years, ext::make_shared<SimpleQuote>(0.04949)},
            {3 * Years, ext::make_shared<SimpleQuote>(0.04598)},
            {4 * Years, ext::make_shared<SimpleQuote>(0.04371)},
            {5 * Years, ext::make_shared<SimpleQuote>(0.04231)},
            {7 * Years, ext::make_shared<SimpleQuote>(0.04068)}
        };
        
        auto sofr = ext::make_shared<Sofr>();
        
        for (const auto& q : longOisQuotes) {
            auto tenor = q.first;
            auto quote = q.second;
            auto helper = ext::make_shared<OISRateHelper>(
                2, tenor, Handle<Quote>(quote), sofr);
            sofrInstruments.push_back(helper);
        }
        auto sofrTermStructure = ext::make_shared<PiecewiseYieldCurve<Discount, LogLinear>>(
                todaysDate, sofrInstruments, termStructureDayCounter);

        sofrTermStructure->enableExtrapolation();
        RelinkableHandle<YieldTermStructure> discountingTermStructure;
        discountingTermStructure.linkTo(sofrTermStructure);

        
        std::vector<Period> swaptionMaturities;
        swaptionMaturities.emplace_back(1, Years);
        swaptionMaturities.emplace_back(2, Years);
        swaptionMaturities.emplace_back(3, Years);
        swaptionMaturities.emplace_back(4, Years);
        swaptionMaturities.emplace_back(5, Years);
        swaptionMaturities.emplace_back(7, Years);

        std::vector<ext::shared_ptr<BlackCalibrationHelper>> swaptions;
        std::list<Time> times;
        
        sofr = ext::make_shared<Sofr>(discountingTermStructure);
        
        Size i;
        for (i=0; i<numRows; i++) {
            Size j = numCols - i -1; // 1x5, 2x4, 3x3, 4x2, 5x1
            Size k = i*numCols + j;
            auto vol = ext::make_shared<SimpleQuote>(swaptionVols[k]);
            swaptions.push_back(ext::make_shared<SwaptionHelper>(
                               swaptionMaturities[i],
                               Period(swapLengths[j], Years),
                               Handle<Quote>(vol),
                               sofr,
                               sofr->tenor(),
                               sofr->dayCounter(),
                               sofr->dayCounter(),
                               discountingTermStructure));
            swaptions.back()->addTimesTo(times);
        }

        // Building time-grid
        TimeGrid grid(times.begin(), times.end(), 30);


        // defining the models
        auto modelG2 = ext::make_shared<G2>(discountingTermStructure);

        // model calibrations

        std::cout << "G2 (analytic formulae) calibration" << std::endl;
        for (i=0; i<swaptions.size(); i++)
            swaptions[i]->setPricingEngine(ext::make_shared<G2SwaptionEngine>(modelG2, 6.0, 16));

        calibrateModel(modelG2, swaptions);
        std::cout << "calibrated to:\n"
                  << "a     = " << modelG2->params()[0] << ", "
                  << "sigma = " << modelG2->params()[1] << "\n"
                  << "b     = " << modelG2->params()[2] << ", "
                  << "eta   = " << modelG2->params()[3] << "\n"
                  << "rho   = " << modelG2->params()[4]
                  << std::endl << std::endl;
        
        // Interest Rate Swap for Making Swaption //
        Swap::Type type = Swap::Payer;
        Rate dummyFixedRate = 0.066;
        auto indexSixMonths = ext::make_shared<Sofr>(discountingTermStructure);

        Frequency fixedLegFrequency = Quarterly;
        BusinessDayConvention fixedLegConvention = ModifiedFollowing;
        BusinessDayConvention floatingLegConvention = ModifiedFollowing;
        DayCounter fixedLegDayCounter =Actual360();
        Frequency floatingLegFrequency = Quarterly;
        
        Date maturity = calendar.advance(settlementDate,3,Years,
                                                floatingLegConvention);
        Schedule fixedSchedule(settlementDate,maturity,Period(fixedLegFrequency),
                                      calendar,fixedLegConvention,fixedLegConvention,
                                      DateGeneration::Forward,false);
        Schedule floatSchedule(settlementDate,maturity,Period(floatingLegFrequency),
                                      calendar,floatingLegConvention,floatingLegConvention,
                                      DateGeneration::Forward,false);

        auto swap = ext::make_shared<VanillaSwap>(
                   type, 10000.0,
                   fixedSchedule, dummyFixedRate, fixedLegDayCounter,
                   floatSchedule, indexSixMonths, 0.0,
                   indexSixMonths->dayCounter());
        swap->setPricingEngine(ext::make_shared<DiscountingSwapEngine>(discountingTermStructure));
        
        std::vector<Date> bermudanDates;
        
        const std::vector<ext::shared_ptr<CashFlow>>& leg =
            swap->fixedLeg();
        for (i=0; i<leg.size(); i++) {
            auto coupon = ext::dynamic_pointer_cast<Coupon>(leg[i]);
            bermudanDates.push_back(coupon->accrualStartDate());
        }
        auto bermudanExercise = ext::make_shared<BermudanExercise>(bermudanDates);

        Swaption bermudanSwaption(swap, bermudanExercise);

        bermudanSwaption.setPricingEngine(ext::make_shared<TreeSwaptionEngine>(modelG2, 50));
        std::cout << "G2 (tree):      " << bermudanSwaption.NPV() << std::endl;
        bermudanSwaption.setPricingEngine(ext::make_shared<FdG2SwaptionEngine>(modelG2));
        std::cout << "G2 (fdm) :      " << bermudanSwaption.NPV() << std::endl;

        

        return 0;
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "unknown error" << std::endl;
        return 1;
    }
}

