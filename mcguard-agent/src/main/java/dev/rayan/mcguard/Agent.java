package dev.rayan.mcguard;

import java.lang.instrument.Instrumentation;

public final class Agent {
    private Agent() {}

    public static void premain(String socketPath, Instrumentation instrumentation) {
        if (socketPath == null || socketPath.isBlank()) {
            fail("missing broker socket");
        }
        BrokerClient.configure(socketPath);
        instrumentation.addTransformer(new AuthlibTransformer());
        System.err.println("[MCGuard] Token protection active; Minecraft received a placeholder credential.");
    }

    static void fail(String reason) {
        System.err.println("[MCGuard] Secure launch failed closed: " + reason);
        Runtime.getRuntime().halt(78);
    }
}
