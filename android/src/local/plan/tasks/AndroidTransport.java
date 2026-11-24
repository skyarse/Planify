package local.plan.tasks;

import android.content.Context;
import android.net.http.HttpEngine;
import android.net.http.HttpException;
import android.net.http.UploadDataProvider;
import android.net.http.UploadDataSink;
import android.net.http.UrlRequest;
import android.net.http.UrlResponseInfo;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.net.URI;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

final class AndroidTransport implements DavClient.Transport, AutoCloseable {
    private final ExecutorService callbacks = Executors.newSingleThreadExecutor();
    private final HttpEngine engine;

    AndroidTransport(Context context) {
        engine = new HttpEngine.Builder(context.getApplicationContext()).setUserAgent("PlanTasks/Android").build();
    }

    @Override
    public DavClient.Response send(String method, URI uri, String depth, String body, String authorization) throws IOException {
        CompletableFuture<DavClient.Response> result = new CompletableFuture<>();
        byte[] data = body.getBytes(StandardCharsets.UTF_8);
        UrlRequest.Builder builder = engine.newUrlRequestBuilder(uri.toASCIIString(), callbacks, new UrlRequest.Callback() {
            final ByteArrayOutputStream output = new ByteArrayOutputStream();
            final byte[] chunk = new byte[8192];
            boolean redirected;
            boolean tooLarge;

            @Override public void onRedirectReceived(UrlRequest request, UrlResponseInfo info, String location) {
                redirected = true;
                request.cancel();
            }
            @Override public void onResponseStarted(UrlRequest request, UrlResponseInfo info) {
                request.read(ByteBuffer.allocateDirect(chunk.length));
            }
            @Override public void onReadCompleted(UrlRequest request, UrlResponseInfo info, ByteBuffer buffer) {
                buffer.flip();
                int size = buffer.remaining();
                if (size > DavClient.MAX_RESPONSE - output.size()) {
                    tooLarge = true;
                    request.cancel();
                    return;
                }
                buffer.get(chunk, 0, size);
                output.write(chunk, 0, size);
                buffer.clear();
                request.read(buffer);
            }
            @Override public void onSucceeded(UrlRequest request, UrlResponseInfo info) {
                List<String> challenges = new ArrayList<>();
                for (Map.Entry<String, String> header : info.getHeaders().getAsList())
                    if ("www-authenticate".equalsIgnoreCase(header.getKey())) challenges.add(header.getValue());
                result.complete(new DavClient.Response(info.getHttpStatusCode(),
                        new String(output.toByteArray(), StandardCharsets.UTF_8), challenges));
            }
            @Override public void onFailed(UrlRequest request, UrlResponseInfo info, HttpException error) {
                result.completeExceptionally(new IOException(
                        "Не удалось подключиться к Baïkal. Проверь адрес, сеть и сертификат сервера.", error));
            }
            @Override public void onCanceled(UrlRequest request, UrlResponseInfo info) {
                if (redirected) result.complete(new DavClient.Response(info.getHttpStatusCode(), "", new ArrayList<>()));
                else result.completeExceptionally(new IOException(tooLarge
                        ? "Ответ Baïkal слишком большой. Предыдущие задачи сохранены." : "Импорт прерван."));
            }
        }).setHttpMethod(method).setCacheDisabled(true)
                .addHeader("Depth", depth).addHeader("Content-Type", "application/xml; charset=utf-8")
                .setUploadDataProvider(new UploadDataProvider() {
                    int offset;
                    @Override public long getLength() { return data.length; }
                    @Override public void read(UploadDataSink sink, ByteBuffer buffer) {
                        int size = Math.min(buffer.remaining(), data.length - offset);
                        buffer.put(data, offset, size);
                        offset += size;
                        sink.onReadSucceeded(false);
                    }
                    @Override public void rewind(UploadDataSink sink) {
                        offset = 0;
                        sink.onRewindSucceeded();
                    }
                }, callbacks);
        if (authorization != null) builder.addHeader("Authorization", authorization);
        UrlRequest request = builder.build();
        request.start();
        try {
            return result.get(25, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            request.cancel();
            Thread.currentThread().interrupt();
            throw new IOException("Импорт прерван.", e);
        } catch (TimeoutException e) {
            request.cancel();
            throw new IOException("Baïkal не ответил вовремя.", e);
        } catch (ExecutionException e) {
            if (e.getCause() instanceof IOException) throw (IOException) e.getCause();
            throw new IOException("Не удалось прочитать ответ Baïkal.", e);
        }
    }

    @Override
    public void close() {
        try {
            engine.shutdown();
        } catch (IllegalStateException ignored) {
            // A cancelled request can still be finishing on the network thread.
        } finally {
            callbacks.shutdown();
        }
    }
}
