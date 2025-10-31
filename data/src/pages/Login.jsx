import { useState } from "react";
import { useNavigate } from "react-router-dom";

export default function Login() {
  const navigate = useNavigate();
  const [user, setUser] = useState({ username: "", password: "" });

  const handleSubmit = e => {
    e.preventDefault();
    // Exemplo de login local (ESP32 poderia validar)
    if (user.username && user.password) {
      navigate("/");
    }
  };

  return (
    <main className="center">
      <h2>Login</h2>
      <form onSubmit={handleSubmit}>
        <input
          placeholder="Usuário"
          value={user.username}
          onChange={e => setUser({ ...user, username: e.target.value })}
        />
        <input
          placeholder="Senha"
          type="password"
          value={user.password}
          onChange={e => setUser({ ...user, password: e.target.value })}
        />
        <button type="submit">Entrar</button>
      </form>
    </main>
  );
}
