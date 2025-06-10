// Copyright (c) ZeroC, Inc.

#include "Ice/Ice.h"
#include "Test.h"
#include "TestHelper.h"

using namespace std;
using namespace Test;

void
allTests(Test::TestHelper* helper)
{
    Ice::CommunicatorPtr communicator = helper->communicator();
    Ice::ObjectPrx admin(communicator, "DemoIceBox/admin:default -p 9996 -t 10000");

    optional<TestFacetPrx> facet;

    cout << "testing custom facet... " << flush;
    {
        //
        // Test: Verify that the custom facet is present.
        //
        facet = admin->ice_facet<Test::TestFacetPrx>("TestFacet");
        facet->ice_ping();
    }
    cout << "ok" << endl;

    cout << "testing properties facet... " << flush;
    {
        auto pa = admin->ice_facet<Ice::PropertiesAdminPrx>("IceBox.Service.TestService.Properties");
        //
        // Test: PropertiesAdmin::getProperty()
        //
        test(pa->getProperty("Prop1") == "1");
        test(pa->getProperty("Bogus") == "");

        //
        // Test: PropertiesAdmin::getProperties()
        //
        Ice::PropertyDict pd = pa->getPropertiesForPrefix("");
        test(pd.size() == 6);
        test(pd["Prop1"] == "1");
        test(pd["Prop2"] == "2");
        test(pd["Prop3"] == "3");
        test(pd["Ice.Config"] == "config.service");
        test(pd["Ice.ProgramName"] == "IceBox-TestService");
        test(pd["Ice.Admin.Enabled"] == "1");

        Ice::PropertyDict changes;

        //
        // Test: PropertiesAdmin::setProperties()
        //
        Ice::PropertyDict setProps;
        setProps["Prop1"] = "10"; // Changed
        setProps["Prop2"] = "20"; // Changed
        setProps["Prop3"] = "";   // Removed
        setProps["Prop4"] = "4";  // Added
        setProps["Prop5"] = "5";  // Added
        pa->setProperties(setProps);
        test(pa->getProperty("Prop1") == "10");
        test(pa->getProperty("Prop2") == "20");
        test(pa->getProperty("Prop3") == "");
        test(pa->getProperty("Prop4") == "4");
        test(pa->getProperty("Prop5") == "5");
        changes = facet->getChanges();
        test(changes.size() == 5);
        test(changes["Prop1"] == "10");
        test(changes["Prop2"] == "20");
        test(changes["Prop3"] == "");
        test(changes["Prop4"] == "4");
        test(changes["Prop5"] == "5");
        pa->setProperties(setProps);
        changes = facet->getChanges();
        test(changes.empty());
    }
    cout << "ok" << endl;

    cout << "testing metrics admin facet... " << flush;
    {
        auto ma = admin->ice_facet<IceMX::MetricsAdminPrx>("IceBox.Service.TestService.Metrics");
        auto pa = admin->ice_facet<Ice::PropertiesAdminPrx>("IceBox.Service.TestService.Properties");
        Ice::StringSeq views;
        Ice::StringSeq disabledViews;
        views = ma->getMetricsViewNames(disabledViews);
        test(views.empty());

        Ice::PropertyDict setProps;
        setProps["IceMX.Metrics.Debug.GroupBy"] = "id";
        setProps["IceMX.Metrics.All.GroupBy"] = "none";
        setProps["IceMX.Metrics.Parent.GroupBy"] = "parent";
        pa->setProperties(setProps);
        pa->setProperties(Ice::PropertyDict());

        views = ma->getMetricsViewNames(disabledViews);
        test(views.size() == 3);

        // Make sure that the IceBox communicator metrics admin is a separate instance.
        test(admin->ice_facet<IceMX::MetricsAdminPrx>("Metrics")->getMetricsViewNames(disabledViews).empty());
    }
    cout << "ok" << endl;
}
