package dev.rayan.mcguard;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.StandardProtocolFamily;
import java.net.UnixDomainSocketAddress;
import java.nio.channels.Channels;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.UUID;

public final class BrokerClient {
    private static volatile String socketPath;

    private BrokerClient() {}

    static void configure(String path) {
        socketPath = path;
    }

    public static void joinServer(UUID profileId, String serverHash) {
        String path = socketPath;
        if (path == null || profileId == null || serverHash == null || serverHash.length() > 64
                || serverHash.indexOf('\n') >= 0 || serverHash.indexOf('\t') >= 0) {
            throw new IllegalStateException("MCGuard rejected an invalid join request");
        }

        String response = request("JOIN\t" + profileId + "\t" + serverHash);
        if (!"OK".equals(response)) {
            throw new IllegalStateException("MCGuard broker rejected session authentication");
        }
    }

    public static Object getKeyPair() {
        String response = request("KEYPAIR");
        if (response == null || !response.startsWith("DATA\t")) {
            throw new IllegalStateException("MCGuard broker rejected profile certificate request");
        }
        try {
            String json = new String(Base64.getDecoder().decode(response.substring(5)), StandardCharsets.UTF_8);
            ClassLoader gameLoader = Thread.currentThread().getContextClassLoader();
            Class<?> mapperClass = Class.forName(
                "com.mojang.authlib.minecraft.client.ObjectMapper", true, gameLoader);
            Object mapper = mapperClass.getMethod("create").invoke(null);
            Class<?> responseClass = Class.forName(
                "com.mojang.authlib.yggdrasil.response.KeyPairResponse", true, gameLoader);
            return mapperClass.getMethod("readValue", String.class, Class.class)
                .invoke(mapper, json, responseClass);
        } catch (ReflectiveOperationException | IllegalArgumentException exception) {
            throw new IllegalStateException("MCGuard could not decode the profile certificate", exception);
        }
    }

    private static String request(String request) {
        String path = socketPath;
        if (path == null) {
            throw new IllegalStateException("MCGuard broker is not configured");
        }
        try (SocketChannel channel = SocketChannel.open(StandardProtocolFamily.UNIX)) {
            channel.connect(UnixDomainSocketAddress.of(path));
            BufferedWriter writer = new BufferedWriter(new OutputStreamWriter(
                Channels.newOutputStream(channel), StandardCharsets.US_ASCII));
            BufferedReader reader = new BufferedReader(new InputStreamReader(
                Channels.newInputStream(channel), StandardCharsets.US_ASCII));
            writer.write(request);
            writer.write('\n');
            writer.flush();

            return reader.readLine();
        } catch (IOException exception) {
            throw new IllegalStateException("MCGuard broker is unavailable", exception);
        }
    }
}
