// Copyright (c) ZeroC, Inc.

import Foundation
import Ice
import TestCommon

func breakRetainCycleB(_ b: B?) {
    if let b1 = b {
        if let b2 = b1.theA as? B {
            b2.theA = nil
            b2.theB?.theA = nil
            b2.theB?.theB = nil
            b2.theB?.theC = nil
            b2.theB = nil
            b2.theC = nil
            // b2 = nil
        }
        b1.theA = nil
        b1.theB = nil
        b1.theC = nil
    }
}

func breakRetainCycleC(_ c: C?) {
    if let c1 = c {
        breakRetainCycleB(c1.theB)
        c1.theB = nil
    }
}

func breakRetainCycleD(_ d: D?) {
    if let d1 = d {
        breakRetainCycleB(d1.theA as? B)
        breakRetainCycleB(d1.theB)
    }
}

func allTests(_ helper: TestHelper) async throws -> InitialPrx {
    func test(_ value: Bool, file: String = #file, line: Int = #line) throws {
        try helper.test(value, file: file, line: line)
    }
    let output = helper.getWriter()

    let communicator = helper.communicator()
    output.write("testing stringToProxy... ")
    var ref = "initial:\(helper.getTestEndpoint(num: 0))"
    var base = try communicator.stringToProxy(ref)!
    output.writeLine("ok")

    output.write("testing checked cast... ")
    let initial = try await checkedCast(prx: base, type: InitialPrx.self)!
    try test(initial == base)
    output.writeLine("ok")

    output.write("getting B1... ")
    var b1 = try await initial.getB1()!
    output.writeLine("ok")

    output.write("getting B2... ")
    let b2 = try await initial.getB2()!
    output.writeLine("ok")

    output.write("getting C... ")
    let c = try await initial.getC()!
    output.writeLine("ok")

    output.write("getting D... ")
    let d = try await initial.getD()!
    output.writeLine("ok")

    output.write("checking consistency... ")
    try test(b1 !== b2)
    // test(b1 != c)
    // test(b1 != d)
    // test(b2 != c)
    // test(b2 != d)
    // test(c != d)
    try test(b1.theB === b1)
    try test(b1.theC == nil)
    try test(b1.theA is B)
    try test((b1.theA as! B).theA === b1.theA)
    try test((b1.theA as! B).theB === b1)
    // test(((B)b1.theA).theC is C) // Redundant -- theC is always of type C
    try test((b1.theA as! B).theC!.theB === b1.theA)
    try test(b1.preMarshalInvoked)
    try test(b1.postUnmarshalInvoked)
    try test(b1.theA!.preMarshalInvoked)
    try test(b1.theA!.postUnmarshalInvoked)
    try test((b1.theA as! B).theC!.preMarshalInvoked)
    try test((b1.theA as! B).theC!.postUnmarshalInvoked)

    // More tests possible for b2 and d, but I think this is already
    // sufficient.
    try test(b2.theA === b2)
    try test(d.theC === nil)

    breakRetainCycleB(b1)
    breakRetainCycleB(b2)
    breakRetainCycleC(c)
    breakRetainCycleD(d)

    output.writeLine("ok")

    output.write("getting B1, B2, C, and D all at once... ")
    let (b1out, b2out, cout, dout) = try await initial.getAll()
    try test(b1out !== nil)
    try test(b2out !== nil)
    try test(cout !== nil)
    try test(dout !== nil)
    output.writeLine("ok")

    output.write("checking consistency... ")
    try test(b1out !== b2out)
    try test(b1out!.theA === b2out)
    try test(b1out!.theB === b1out)
    try test(b1out!.theC === nil)
    try test(b2out!.theA === b2out)
    try test(b2out!.theB === b1out)
    try test(b2out!.theC === cout)
    try test(cout!.theB === b2out)
    try test(dout!.theA === b1out)
    try test(dout!.theB === b2out)
    try test(dout!.theC === nil)
    try test(dout!.preMarshalInvoked)
    try test(dout!.postUnmarshalInvoked)
    try test(dout!.theA!.preMarshalInvoked)
    try test(dout!.theA!.postUnmarshalInvoked)
    try test(dout!.theB!.preMarshalInvoked)
    try test(dout!.theB!.postUnmarshalInvoked)
    try test(dout!.theB!.theC!.preMarshalInvoked)
    try test(dout!.theB!.theC!.postUnmarshalInvoked)

    breakRetainCycleB(b1out)
    breakRetainCycleB(b2out)
    breakRetainCycleC(cout)
    breakRetainCycleD(dout)

    output.writeLine("ok")

    output.write("getting K... ")
    let k = try await initial.getK()!
    let l = k.value as! L
    try test(l.data == "l")
    output.writeLine("ok")

    output.write("testing Value as parameter... ")
    do {
        let (v3, v2) = try await initial.opValue(L(data: "l"))
        try test((v2 as! L).data == "l")
        try test((v3 as! L).data == "l")
    }
    do {
        let (v3, v2) = try await initial.opValueSeq([L(data: "l")])
        try test((v2[0] as! L).data == "l")
        try test((v3[0] as! L).data == "l")
    }
    do {
        let (v3, v2) = try await initial.opValueMap(["l": L(data: "l")])
        try test((v2["l"]! as! L).data == "l")
        try test((v3["l"]! as! L).data == "l")
    }
    output.writeLine("ok")

    output.write("getting D1... ")
    do {
        var d1 = D1(
            a1: A1(name: "a1"),
            a2: A1(name: "a2"),
            a3: A1(name: "a3"),
            a4: A1(name: "a4"))
        d1 = try await initial.getD1(d1)!
        try test(d1.a1!.name == "a1")
        try test(d1.a2!.name == "a2")
        try test(d1.a3!.name == "a3")
        try test(d1.a4!.name == "a4")
    }
    output.writeLine("ok")

    output.write("throw EDerived... ")
    do {
        try await initial.throwEDerived()
        try test(false)
    } catch let ederived as EDerived {
        try test(ederived.a1!.name == "a1")
        try test(ederived.a2!.name == "a2")
        try test(ederived.a3!.name == "a3")
        try test(ederived.a4!.name == "a4")
    }
    output.writeLine("ok")

    output.write("setting G... ")
    do {
        try await initial.setG(G(theS: S(str: "hello"), str: "g"))
    } catch is Ice.OperationNotExistException {}
    output.writeLine("ok")

    output.write("testing sequences...")
    do {
        var (retS, outS) = try await initial.opBaseSeq([Base]())
        (retS, outS) = try await initial.opBaseSeq([Base(theS: S(), str: "")])
        try test(retS.count == 1 && outS.count == 1)
    } catch is Ice.OperationNotExistException {}
    output.writeLine("ok")

    output.write("testing recursive type... ")
    let top = Recursive()
    var bottom = top
    for _ in 1..<10 {
        bottom.v = Recursive()
        bottom = bottom.v!
    }
    try await initial.setRecursive(top)

    // Adding one more level would exceed the max class graph depth
    bottom.v = Recursive()
    bottom = bottom.v!
    do {
        try await initial.setRecursive(top)
        try test(false)
    } catch is Ice.UnknownLocalException {
        // Expected marshal exception from the server(max class graph depth reached)
    }
    try await initial.setRecursive(Recursive())
    output.writeLine("ok")

    output.write("testing compact ID...")
    do {
        try await test(initial.getCompact() != nil)
    } catch is Ice.OperationNotExistException {}
    output.writeLine("ok")

    output.write("testing marshaled results...")
    b1 = try await initial.getMB()!
    try test(b1.theB === b1)
    breakRetainCycleB(b1)
    b1 = try await initial.getAMDMB()!
    try test(b1.theB === b1)
    breakRetainCycleB(b1)
    output.writeLine("ok")

    output.write("testing UnexpectedObjectException...")
    ref = "uoet:\(helper.getTestEndpoint(num: 0))"
    base = try communicator.stringToProxy(ref)!
    let uoet = uncheckedCast(prx: base, type: UnexpectedObjectExceptionTestPrx.self)
    do {
        _ = try await uoet.op()
        try test(false)
    } catch let ex as Ice.MarshalException {
        try test(ex.message.contains("::Test::AlsoEmpty"))
        try test(ex.message.contains("::Test::Empty"))
    } catch {
        output.writeLine("\(error)")
        try test(false)
    }
    output.writeLine("ok")

    output.write("testing class containing complex dictionary... ")
    do {
        let k1 = StructKey(i: 1, s: "1")
        let k2 = StructKey(i: 2, s: "2")
        let (m2, m1) = try await initial.opM(M(v: [k1: L(data: "one"), k2: L(data: "two")]))
        try test(m1!.v.count == 2)
        try test(m2!.v.count == 2)

        try test(m1!.v[k1]!!.data == "one")
        try test(m2!.v[k1]!!.data == "one")

        try test(m1!.v[k2]!!.data == "two")
        try test(m2!.v[k2]!!.data == "two")
    }
    output.writeLine("ok")

    output.write("testing forward declarations... ")
    do {
        let (f11, f12) = try await initial.opF1(F1(name: "F11"))
        try test(f11!.name == "F11")
        try test(f12!.name == "F12")

        ref = "F21:\(helper.getTestEndpoint(num: 0))"
        let (f21, f22) = try await initial.opF2(
            uncheckedCast(
                prx: communicator.stringToProxy(ref)!,
                type: F2Prx.self))
        try test(f21!.ice_getIdentity().name == "F21")
        try await f21!.op()
        try test(f22!.ice_getIdentity().name == "F22")

        if try await initial.hasF3() {
            let (f31, f32) = try await initial.opF3(F3(f1: f11, f2: f21))
            try test(f31!.f1!.name == "F11")
            try test(f31!.f2!.ice_getIdentity().name == "F21")

            try test(f32!.f1!.name == "F12")
            try test(f32!.f2!.ice_getIdentity().name == "F22")
        }
    }
    output.writeLine("ok")

    output.write("testing sending class cycle... ")
    do {
        let rec = Recursive(v: nil)
        rec.v = rec
        let acceptsCycles = try await initial.acceptsClassCycles()
        do {
            try await initial.setCycle(rec)
            try test(acceptsCycles)
        } catch is Ice.UnknownLocalException {
            try test(!acceptsCycles)
        }
        rec.v = nil
    }
    output.writeLine("ok")

    return initial
}
